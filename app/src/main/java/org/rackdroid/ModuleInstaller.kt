package org.rackdroid

import android.app.Activity
import java.io.File

/** Stable facade used by MainActivity/native startup. Package parsing, storage and transactions live separately. */
class ModuleActivationRestartRequiredException(message: String) : RuntimeException(message)

object ModuleInstaller {
    private val lock = Any()
    private enum class NativeLoadOutcome { LOADED, ALREADY_LOADED, SYSTEM_LOAD_FAILED, REGISTRATION_FAILED }

    fun modulesDir(activity: Activity): File = ModulePackageStore.externalModules(activity)
    fun inboxDestination(activity: Activity, requestedName: String): File = ModulePackageStore.uniqueInboxFile(activity, requestedName)
    fun inspectPack(pack: File): ModulePackageDescriptor = ModulePackageParser.inspect(pack)

    fun installedPacks(activity: Activity): List<InstalledModulePack> =
        ModulePackageStore.installedRoot(activity).listFiles { f ->
            f.isDirectory &&
                ModulePackageParser.isValidSlug(f.name) &&
                !f.name.endsWith(".backup") &&
                !f.name.endsWith(".update.tmp")
        }
            ?.map { dir ->
                val meta = ModulePackageStore.readJson(ModulePackageStore.metadataFile(activity, dir.name))
                InstalledModulePack(dir.name, dir, dir.walkTopDown().filter { it.isFile }.map { it.length() }.sum(),
                    meta?.optString("version")?.takeIf { it.isNotBlank() },
                    ModuleTransaction.hasPending(activity, dir.name), ModulePackageStore.backup(activity, dir.name).isDirectory)
            }?.sortedBy { it.slug.lowercase() } ?: emptyList()

    /** Compatibility wrapper for callers that install one staged file. */
    fun installIncoming(
        activity: Activity,
        input: File,
        allowDowngrade: Boolean = false
    ): List<ModuleInstallResult> =
        installIncomingBatch(activity, listOf(input), allowDowngrade)

    /**
     * Manual picker path. The entire multi-select is preflighted as one request
     * so conflicting packages with the same slug cannot be partially applied in
     * picker order. Valid packages are still installed independently after
     * preflight; one package's transaction failure does not corrupt another.
     *
     * When downgrade confirmation is required, staged inputs are deliberately
     * retained so the exact same request can be retried after consent.
     */
    fun installIncomingBatch(
        activity: Activity,
        inputs: List<File>,
        allowDowngrade: Boolean = false
    ): List<ModuleInstallResult> = synchronized(lock) {
        if (inputs.isEmpty()) return@synchronized emptyList()

        val rootWork = File(ModulePackageStore.work(activity), "batch-${System.nanoTime()}")
        val descriptors = ArrayList<ModulePackageDescriptor>()
        val failures = ArrayList<ModuleInstallResult>()

        try {
            rootWork.mkdirs()

            // Full preflight of every selected document before installing any.
            inputs.forEachIndexed { index, input ->
                val work = File(rootWork, "input-$index")
                try {
                    val packs = ModulePackageParser.expandInput(input, work)
                    descriptors += packs.map { ModulePackageParser.inspect(it) }
                } catch (t: Throwable) {
                    failures += ModuleInstallResult(
                        ModuleInstallStatus.FAILED,
                        null,
                        input.nameWithoutExtension,
                        "${input.name} could not be imported: ${t.message ?: "unknown error"}."
                    )
                }
            }

            val conflicts = descriptors.groupBy { it.slug }
                .filterValues { group ->
                    group.map { it.sha256 }.distinct().size > 1
                }

            // Do not let picker ordering choose a winner for duplicate slug content.
            if (conflicts.isNotEmpty()) {
                conflicts.forEach { (slug, group) ->
                    failures += ModuleInstallResult(
                        ModuleInstallStatus.CONFLICT,
                        slug,
                        group.first().title,
                        "The selected files contain different packages claiming slug $slug; nothing for that slug was installed."
                    )
                }
            }

            val installable = descriptors
                .filterNot { conflicts.containsKey(it.slug) }
                .distinctBy { it.slug }

            if (!allowDowngrade) {
                val downgrades = installable.filter {
                    ModuleTransaction.requiresDowngrade(activity, it)
                }
                if (downgrades.isNotEmpty()) {
                    val downgradeResults = downgrades.map { d ->
                        ModuleInstallResult(
                            ModuleInstallStatus.DOWNGRADE_REQUIRES_CONFIRMATION,
                            d.slug,
                            d.title,
                            "${d.title} ${d.version ?: ""} is older than the installed version. Confirm to continue."
                        )
                    }
                    // No installation occurs before downgrade consent. Keep every
                    // staged input so the exact batch can be replayed.
                    return@synchronized failures + downgradeResults
                }
            }

            val results = ArrayList<ModuleInstallResult>()
            results += failures
            for (d in installable) {
                results += ModuleTransaction.install(activity, d, allowDowngrade)
            }

            // These are private staging copies. The original SAF documents are untouched.
            inputs.forEach { it.delete() }
            results
        } catch (t: Throwable) {
            inputs.forEach { it.delete() }
            failures + ModuleInstallResult(
                ModuleInstallStatus.FAILED,
                null,
                "Module packs",
                "Module pack import failed: ${t.message ?: "unknown error"}."
            )
        } finally {
            rootWork.deleteRecursively()
        }
    }

    /**
     * Native startup entry point. Historical files in external Modules/ are migrated once.
     * Existing installed slugs are NEVER silently updated from that legacy folder.
     */
    fun loadUserPlugins(activity: Activity): Int = synchronized(lock) {
        recoverLegacyInstallArtifacts(activity)
        ModuleTransaction.recoverInterrupted(activity)
        migrateLegacyDropFolder(activity)
        var loaded = 0
        val dirs = ModulePackageStore.installedRoot(activity).listFiles { f ->
            f.isDirectory &&
                ModulePackageParser.isValidSlug(f.name) &&
                !f.name.endsWith(".backup") &&
                !f.name.endsWith(".update.tmp")
        }?.sortedBy { it.name.lowercase() } ?: emptyList()
        for (dir in dirs) if (loadInstalledWithRollback(activity, dir)) loaded++
        loaded
    }

    /** Previous launch died before patchReady. Pending activations are the first rollback target. */
    fun rollbackPendingAfterFailedStartup(activity: Activity): Int = synchronized(lock) {
        ModuleTransaction.recoverInterrupted(activity)
        ModuleTransaction.rollbackAllPending(activity, "RackDroid did not complete startup after module activation")
    }

    /** Called only once native startup and patch restore have both succeeded. */
    fun commitPendingActivations(activity: Activity): Int = synchronized(lock) { ModuleTransaction.commitAllPending(activity) }

    fun uninstall(activity: Activity, slug: String): Boolean = synchronized(lock) {
        if (!ModulePackageParser.isValidSlug(slug)) return@synchronized false
        var removed = false
        val installed = ModulePackageStore.installed(activity, slug)
        if (installed.exists()) { installed.deleteRecursively(); removed = true }
        val backup = ModulePackageStore.backup(activity, slug)
        if (backup.exists()) { backup.deleteRecursively(); removed = true }
        if (ModulePackageStore.transactionFile(activity, slug).delete()) removed = true
        if (ModulePackageStore.metadataFile(activity, slug).delete()) removed = true
        removed
    }

    private fun loadInstalledWithRollback(activity: Activity, dir: File): Boolean {
        val outcome = loadInstalled(activity, dir)
        if (outcome == NativeLoadOutcome.LOADED || outcome == NativeLoadOutcome.ALREADY_LOADED) return true
        val slug = dir.name
        if (!ModuleTransaction.hasPending(activity, slug)) return false

        // A failed System.load means the candidate never entered this process;
        // restoring and loading the old .so immediately is safe. If System.load
        // succeeded but Rack registration failed, the candidate library is now
        // resident and cannot be unloaded safely. Restore files on disk, then
        // restart the process before the autosaved patch is restored.
        if (!ModuleTransaction.rollback(activity, slug, "new native plugin failed activation")) return false
        if (outcome == NativeLoadOutcome.REGISTRATION_FAILED)
            throw ModuleActivationRestartRequiredException("$slug was rolled back after native registration failed")

        val restored = ModulePackageStore.installed(activity, slug)
        return restored.isDirectory && loadInstalled(activity, restored) == NativeLoadOutcome.LOADED
    }

    private fun loadInstalled(activity: Activity, dir: File): NativeLoadOutcome {
        val so = try { ModulePackageParser.validateInstalledLayout(dir, dir.name) }
        catch (t: Throwable) { log("installed ${dir.name} invalid: ${t.message}"); return NativeLoadOutcome.SYSTEM_LOAD_FAILED }
        val main = activity as MainActivity
        if (main.isPluginLoadedNative(dir.name)) return NativeLoadOutcome.ALREADY_LOADED
        runCatching { so.setReadOnly() }
        try {
            System.load(so.absolutePath)
        } catch (t: Throwable) {
            log("System.load ${dir.name}/${so.name} failed: ${t.message}")
            return NativeLoadOutcome.SYSTEM_LOAD_FAILED
        }
        return if (main.loadUserPluginNative(dir.absolutePath, so.name)) NativeLoadOutcome.LOADED
        else { log("native registration ${dir.name}/${so.name} failed"); NativeLoadOutcome.REGISTRATION_FAILED }
    }

    /**
     * Previous experimental installers used <slug>.backup and
     * <slug>.update.tmp inside user/plugins. Those suffixes are legal under the
     * slug regex, so without an explicit migration they could be mistaken for
     * third-party plugins. Recover/remove them before enumerating installed slugs.
     */
    private fun recoverLegacyInstallArtifacts(activity: Activity) {
        val root = ModulePackageStore.installedRoot(activity)

        root.listFiles { f -> f.isDirectory && f.name.endsWith(".update.tmp") }
            ?.forEach { tmp ->
                tmp.deleteRecursively()
                log("removed legacy temporary module directory ${tmp.name}")
            }

        root.listFiles { f -> f.isDirectory && f.name.endsWith(".backup") }
            ?.forEach { oldBackup ->
                val slug = oldBackup.name.removeSuffix(".backup")
                if (!ModulePackageParser.isValidSlug(slug)) {
                    return@forEach
                }
                val dest = ModulePackageStore.installed(activity, slug)
                if (!dest.exists()) {
                    if (oldBackup.renameTo(dest)) {
                        log("restored legacy backup for $slug")
                    }
                } else {
                    // A normal install exists, so this is an orphan from the
                    // previous updater, NOT an active rollback generation. Archive
                    // it outside both user/plugins and the active backups directory
                    // so it cannot block a future third-party update.
                    val archiveRoot = ModulePackageStore.legacyBackups(activity)
                    var archived = false
                    for (i in 0..9999) {
                        val candidate = File(
                            archiveRoot,
                            if (i == 0) slug else "$slug ($i)"
                        )
                        if (!candidate.exists()) {
                            archived = oldBackup.renameTo(candidate)
                            if (!archived) {
                                runCatching {
                                    oldBackup.copyRecursively(candidate, overwrite = false)
                                    oldBackup.deleteRecursively()
                                    archived = true
                                }
                            }
                            break
                        }
                    }
                    if (!archived) {
                        log("could not archive legacy backup for $slug")
                    }
                }
            }
    }

    private fun migrateLegacyDropFolder(activity: Activity) {
        val dir = modulesDir(activity)
        writeModulesReadme(dir)
        val processed = File(dir, "Processed").apply { mkdirs() }
        val pending = File(dir, "PendingUpdates").apply { mkdirs() }
        val failed = File(dir, "Failed").apply { mkdirs() }
        val sources = dir.listFiles { f -> f.isFile && (f.name.endsWith(".rdmod", true) || f.name.endsWith(".zip", true)) } ?: return
        for (source in sources) {
            val work = File(ModulePackageStore.work(activity), "legacy-${System.nanoTime()}")
            try {
                val packs = ModulePackageParser.expandInput(source, work)
                val descriptors = packs.map { ModulePackageParser.inspect(it) }
                val wouldUpdate = descriptors.any { d ->
                    val dest = ModulePackageStore.installed(activity, d.slug)
                    if (!dest.isDirectory) false
                    else {
                        val metaHash = ModulePackageStore.readJson(ModulePackageStore.metadataFile(activity, d.slug))?.optString("sha256")
                        if (!metaHash.isNullOrBlank()) metaHash != d.sha256
                        else {
                            val staged = File(work, "compare-${d.slug}")
                            ModulePackageParser.extractAndValidate(d.sourceFile, staged, d.slug)
                            ModulePackageParser.directoryDigest(dest) != ModulePackageParser.directoryDigest(staged)
                        }
                    }
                }
                if (wouldUpdate) {
                    moveUnique(source, pending)
                    log("legacy source ${source.name} contains an update; moved to PendingUpdates instead of auto-updating")
                    continue
                }
                val slugConflicts = descriptors.groupBy { it.slug }.values.any { g -> g.map { it.sha256 }.distinct().size > 1 }
                if (slugConflicts) throw IllegalArgumentException("bundle contains conflicting packages with the same slug")
                // New plugins and byte-identical legacy sources are safe to consume.
                val results = descriptors.distinctBy { it.slug }.map { ModuleTransaction.install(activity, it) }
                if (results.any { it.status == ModuleInstallStatus.FAILED || it.status == ModuleInstallStatus.CONFLICT })
                    throw IllegalStateException(results.joinToString("; ") { it.message })
                moveUnique(source, processed)
            } catch (t: Throwable) {
                log("legacy source ${source.name} failed migration: ${t.message}")
                moveUnique(source, failed)
            } finally { work.deleteRecursively() }
        }
    }

    private fun moveUnique(source: File, targetDir: File) {
        targetDir.mkdirs(); val dot = source.name.lastIndexOf('.'); val stem = if (dot > 0) source.name.substring(0, dot) else source.name; val ext = if (dot > 0) source.name.substring(dot) else ""
        for (i in 0..9999) {
            val dest = File(targetDir, if (i == 0) source.name else "$stem ($i)$ext")
            if (!dest.exists()) { if (!source.renameTo(dest)) { source.copyTo(dest, overwrite = false); source.delete() }; return }
        }
        throw IllegalStateException("too many migrated files with the same name")
    }

    private fun writeModulesReadme(dir: File) {
        val f = File(dir, "README.txt")
        runCatching {
            f.writeText(
                "RackDroid module package inbox\n\n" +
                "Use Module Manager > Install from file for normal installs and updates.\n" +
                "Loose .rdmod files placed directly in this folder are consumed once on startup.\n" +
                "Historical files that would silently replace an installed slug are moved to PendingUpdates instead.\n" +
                "Processed and Failed contain migrated source archives; installed plugin binaries live in private app storage.\n"
            )
        }
    }

    private fun log(s: String) = android.util.Log.i("rackdroid.modules", s)
}
