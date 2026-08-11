package org.rackdroid

import android.app.Activity
import android.os.StatFs
import java.io.File
import org.json.JSONObject

object ModuleTransaction {
    private const val STATE_PREPARED = "PREPARED"
    private const val STATE_OLD_BACKED_UP = "OLD_BACKED_UP"
    private const val STATE_PENDING = "PENDING_ACTIVATION"
    private const val STATE_ACTIVE = "ACTIVE"

    @Synchronized
    fun install(
        activity: Activity,
        descriptor: ModulePackageDescriptor,
        allowDowngrade: Boolean = false
    ): ModuleInstallResult {
        val slug = descriptor.slug
        val title = descriptor.title
        val dest = ModulePackageStore.installed(activity, slug)
        val backup = ModulePackageStore.backup(activity, slug)
        val txn = ModulePackageStore.transactionFile(activity, slug)
        val metaFile = ModulePackageStore.metadataFile(activity, slug)
        val oldMeta = ModulePackageStore.readJson(metaFile)

        if (txn.exists()) {
            return ModuleInstallResult(
                ModuleInstallStatus.CONFLICT, slug, title,
                "$title already has an update pending restart.",
                restartRequired = true
            )
        }

        val knownHash = oldMeta?.optString("sha256")?.takeIf { it.isNotBlank() }
        if (dest.isDirectory && knownHash == descriptor.sha256) {
            return ModuleInstallResult(
                ModuleInstallStatus.ALREADY_INSTALLED, slug, title,
                "$title is already installed."
            )
        }

        val oldVersion = oldMeta?.optString("version")?.takeIf { it.isNotBlank() }
        if (
            dest.isDirectory && !allowDowngrade &&
            oldVersion != null && descriptor.version != null &&
            compareVersionsIfSafe(descriptor.version, oldVersion) == -1
        ) {
            return ModuleInstallResult(
                ModuleInstallStatus.DOWNGRADE_REQUIRES_CONFIRMATION, slug, title,
                "$title ${descriptor.version} is older than installed version $oldVersion."
            )
        }

        val txRoot = File(ModulePackageStore.work(activity), "txn-$slug-${System.nanoTime()}")
        val staged = File(txRoot, "new")
        val hadPrevious = dest.isDirectory

        try {
            txRoot.mkdirs()
            requireFreeSpace(activity, descriptor.sourceFile.length() * 3L + 32L * 1024 * 1024)
            ModulePackageParser.extractAndValidate(descriptor.sourceFile, staged, slug)

            // One-time migration path for installs created before package metadata existed.
            if (
                hadPrevious && knownHash == null &&
                ModulePackageParser.directoryDigest(dest) ==
                    ModulePackageParser.directoryDigest(staged)
            ) {
                writeMetadata(activity, descriptor, pending = false)
                File(dest, ".rdmod-source.sha256").delete()
                return ModuleInstallResult(
                    ModuleInstallStatus.ALREADY_INSTALLED, slug, title,
                    "$title is already installed."
                )
            }

            // Never destroy an existing rollback generation until a new transaction
            // is known to be starting from a stable installed state.
            if (backup.exists()) {
                throw IllegalStateException(
                    "a previous rollback copy still exists for $slug; restart RackDroid before updating again"
                )
            }

            // Save previous metadata inside the transaction so rollback restores
            // not only the old files but also hash/version/provenance state.
            val journal = JSONObject()
                .put("slug", slug)
                .put("title", title)
                .put("version", descriptor.version ?: JSONObject.NULL)
                .put("sha256", descriptor.sha256)
                .put("abi", descriptor.abi)
                .put("library", descriptor.nativeLibraryName)
                .put("hadPrevious", hadPrevious)
                .put("oldMetadata", oldMeta ?: JSONObject.NULL)
                .put("state", STATE_PREPARED)
                .put("createdAt", System.currentTimeMillis())

            // Durable journal BEFORE the first destructive rename.
            ModulePackageStore.writeJsonAtomic(txn, journal)

            if (hadPrevious) {
                if (!dest.renameTo(backup)) {
                    // Old installation is still in place. Clearing the journal is safe.
                    txn.delete()
                    throw IllegalStateException("could not preserve previous version")
                }
                journal.put("state", STATE_OLD_BACKED_UP)
                    .put("backedUpAt", System.currentTimeMillis())
                ModulePackageStore.writeJsonAtomic(txn, journal)
            }

            if (!staged.renameTo(dest)) {
                // Restore using the same recovery routine. It checks that the backup
                // actually exists before touching the current destination.
                rollback(activity, slug, "could not activate staged files")
                throw IllegalStateException("could not activate staged files")
            }

            writeMetadata(activity, descriptor, pending = true)
            journal.put("state", STATE_PENDING)
                .put("pendingAt", System.currentTimeMillis())
            ModulePackageStore.writeJsonAtomic(txn, journal)

            return ModuleInstallResult(
                if (hadPrevious) ModuleInstallStatus.UPDATED else ModuleInstallStatus.INSTALLED,
                slug,
                title,
                if (hadPrevious)
                    "$title updated successfully. Restart RackDroid to activate the new version."
                else
                    "$title installed successfully. Restart RackDroid to activate it.",
                restartRequired = true
            )
        } catch (t: Throwable) {
            // If a journal survived, recover according to its durable state.
            if (txn.exists()) {
                runCatching { rollback(activity, slug, "install transaction failed: ${t.message}") }
            }
            val kept = ModulePackageStore.installed(activity, slug).isDirectory
            return ModuleInstallResult(
                ModuleInstallStatus.FAILED,
                slug,
                title,
                "$title install/update failed: ${t.message ?: "unknown error"}." +
                    if (kept && hadPrevious) " Previous version was kept." else ""
            )
        } finally {
            txRoot.deleteRecursively()
        }
    }

    /**
     * Return the filesystem + metadata to the pre-transaction state.
     *
     * Critical invariant: if the transaction says there was a previous version,
     * we NEVER delete the current destination unless the backup is confirmed to
     * exist first. This prevents a damaged/interrupted journal from destroying
     * the only working copy.
     */
    @Synchronized
    fun rollback(activity: Activity, slug: String, reason: String): Boolean {
        val txnFile = ModulePackageStore.transactionFile(activity, slug)
        val txn = ModulePackageStore.readJson(txnFile) ?: return false
        val state = txn.optString("state")
        if (state !in setOf(STATE_PREPARED, STATE_OLD_BACKED_UP, STATE_PENDING))
            return false

        val dest = ModulePackageStore.installed(activity, slug)
        val backup = ModulePackageStore.backup(activity, slug)
        val metaFile = ModulePackageStore.metadataFile(activity, slug)
        val hadPrevious = txn.optBoolean("hadPrevious", false)
        val oldMeta = txn.opt("oldMetadata") as? JSONObject

        if (hadPrevious) {
            when {
                backup.isDirectory -> {
                    // Whether the candidate is absent or already moved into dest,
                    // backup is our confirmed last-known-good copy.
                    if (dest.exists() && !dest.deleteRecursively()) return false
                    if (!backup.renameTo(dest)) return false
                }
                state == STATE_PREPARED && dest.isDirectory -> {
                    // Crash happened after journal creation but BEFORE old->backup.
                    // No filesystem mutation occurred; keep the old working plugin.
                }
                else -> {
                    // Never delete dest when the promised backup is missing.
                    log("cannot safely roll back $slug: previous-version backup is missing")
                    return false
                }
            }

            if (oldMeta != null) {
                ModulePackageStore.writeJsonAtomic(metaFile, oldMeta)
            } else {
                metaFile.delete()
            }
        } else {
            // Fresh installation: there was no previous plugin to preserve.
            if (state != STATE_PREPARED || dest.isDirectory) {
                dest.deleteRecursively()
            }
            metaFile.delete()
            backup.deleteRecursively()
        }

        txnFile.delete()
        log("rolled back $slug: $reason")
        return true
    }

    @Synchronized
    fun rollbackAllPending(activity: Activity, reason: String): Int {
        var count = 0
        pendingSlugs(activity).forEach {
            if (rollback(activity, it, reason)) count++
        }
        return count
    }

    @Synchronized
    fun commitAllPending(activity: Activity): Int {
        var count = 0
        for (slug in pendingSlugs(activity)) {
            val txnFile = ModulePackageStore.transactionFile(activity, slug)
            val txn = ModulePackageStore.readJson(txnFile) ?: continue
            if (txn.optString("state") != STATE_PENDING) continue

            // Commit marker becomes durable BEFORE deleting rollback data.
            txn.put("state", STATE_ACTIVE)
                .put("activatedAt", System.currentTimeMillis())
            ModulePackageStore.writeJsonAtomic(txnFile, txn)

            val metaFile = ModulePackageStore.metadataFile(activity, slug)
            val meta = ModulePackageStore.readJson(metaFile) ?: JSONObject()
                .put("slug", slug)
                .put("title", txn.optString("title", slug))
                .put("version", txn.opt("version") ?: JSONObject.NULL)
                .put("sha256", txn.optString("sha256"))
                .put("abi", txn.optString("abi"))
                .put("library", txn.optString("library"))

            meta.put("pending", false)
                .put("activatedAt", System.currentTimeMillis())
            ModulePackageStore.writeJsonAtomic(metaFile, meta)

            ModulePackageStore.backup(activity, slug).deleteRecursively()
            txnFile.delete()
            count++
        }
        return count
    }

    /**
     * Recover process death at any transaction boundary.
     *
     * PREPARED:
     *  - dest present, backup absent => first rename never happened; keep dest.
     *  - backup present => destructive work began; restore backup.
     * OLD_BACKED_UP:
     *  - backup is known-good; restore it regardless of whether candidate reached dest.
     * PENDING:
     *  - leave it pending. It must survive native load + patchReady before commit.
     * ACTIVE:
     *  - activation already committed; finish cleanup only.
     */
    @Synchronized
    fun recoverInterrupted(activity: Activity): Int {
        var count = 0
        ModulePackageStore.transactions(activity)
            .listFiles { f -> f.isFile && f.name.endsWith(".json") }
            ?.forEach { file ->
                val j = ModulePackageStore.readJson(file) ?: return@forEach
                val slug = j.optString("slug")
                if (!ModulePackageParser.isValidSlug(slug)) return@forEach
                when (j.optString("state")) {
                    STATE_PREPARED, STATE_OLD_BACKED_UP -> {
                        if (rollback(activity, slug, "interrupted module transaction")) count++
                    }
                    STATE_ACTIVE -> {
                        ModulePackageStore.backup(activity, slug).deleteRecursively()
                        file.delete()
                    }
                }
            }
        return count
    }

    fun requiresDowngrade(
        activity: Activity,
        descriptor: ModulePackageDescriptor
    ): Boolean {
        val oldVersion =
            ModulePackageStore.readJson(
                ModulePackageStore.metadataFile(activity, descriptor.slug)
            )?.optString("version")?.takeIf { it.isNotBlank() } ?: return false
        val newVersion = descriptor.version ?: return false
        return compareVersionsIfSafe(newVersion, oldVersion) == -1
    }

    fun pendingSlugs(activity: Activity): List<String> =
        ModulePackageStore.transactions(activity)
            .listFiles { f -> f.isFile && f.name.endsWith(".json") }
            ?.mapNotNull { f ->
                ModulePackageStore.readJson(f)
                    ?.takeIf { it.optString("state") == STATE_PENDING }
                    ?.optString("slug")
                    ?.takeIf { ModulePackageParser.isValidSlug(it) }
            } ?: emptyList()

    fun hasPending(activity: Activity, slug: String): Boolean =
        slug in pendingSlugs(activity)

    private fun writeMetadata(
        activity: Activity,
        d: ModulePackageDescriptor,
        pending: Boolean
    ) {
        ModulePackageStore.writeJsonAtomic(
            ModulePackageStore.metadataFile(activity, d.slug),
            JSONObject()
                .put("slug", d.slug)
                .put("title", d.title)
                .put("version", d.version ?: JSONObject.NULL)
                .put("sha256", d.sha256)
                .put("abi", d.abi)
                .put("library", d.nativeLibraryName)
                .put("pending", pending)
                .put("installedAt", System.currentTimeMillis())
        )
    }

    private fun requireFreeSpace(activity: Activity, needed: Long) {
        val stat = StatFs(activity.filesDir.absolutePath)
        if (stat.availableBytes < needed) {
            throw IllegalStateException(
                "not enough free storage for a safe transactional install"
            )
        }
    }

    /**
     * Version ordering is intentionally conservative. Only plain numeric
     * versions such as 2, 2.0, 2.0.42 (optionally prefixed with v) are ordered.
     * Vendor-specific/pre-release strings are treated as incomparable instead
     * of risking a false downgrade block.
     *
     * Returns -1, 0, 1, or null when ordering is not safe.
     */
    private fun compareVersionsIfSafe(a: String, b: String): Int? {
        val pattern = Regex("""^v?\d+(?:\.\d+)*$""", RegexOption.IGNORE_CASE)
        val aaRaw = a.trim()
        val bbRaw = b.trim()
        if (!pattern.matches(aaRaw) || !pattern.matches(bbRaw)) return null
        val aa = aaRaw.removePrefix("v").removePrefix("V")
            .split('.').map { it.toLongOrNull() ?: return null }
        val bb = bbRaw.removePrefix("v").removePrefix("V")
            .split('.').map { it.toLongOrNull() ?: return null }
        val n = maxOf(aa.size, bb.size)
        for (i in 0 until n) {
            val x = aa.getOrElse(i) { 0L }
            val y = bb.getOrElse(i) { 0L }
            if (x != y) return x.compareTo(y)
        }
        return 0
    }

    private fun log(s: String) =
        android.util.Log.i("rackdroid.modules", s)
}
