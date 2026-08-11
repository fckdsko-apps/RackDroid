package org.rackdroid

import android.app.Activity
import android.os.Build
import android.widget.Toast
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.OutputStream
import java.io.RandomAccessFile
import java.security.MessageDigest
import java.util.zip.ZipInputStream
import org.json.JSONObject

/**
 * On-demand / side-loadable RackDroid module packs.
 *
 * A .rdmod is a zip containing, at its top level:
 *   plugin.json
 *   res/...
 *   libplugin_<name>.so
 *
 * Packs are copied into the app-visible Modules folder, extracted into
 * filesDir/user/plugins/<slug>, and their native libraries are loaded from
 * private app storage.
 *
 * Existing packs are updated transactionally:
 *   new pack -> validate in temporary directory -> old install -> backup
 *   -> new install becomes live-on-disk -> backup removed.
 *
 * If any replacement step fails, the old pack is restored.
 * A loaded native library cannot safely be replaced in the current process,
 * so an updated pack takes effect after RackDroid restarts.
 */
object ModuleInstaller {
    private const val MAX_PACK_BYTES = 256L * 1024 * 1024
    private const val MAX_MANIFEST_BYTES = 1024L * 1024
    private const val MAX_ENTRY_BYTES = 128L * 1024 * 1024
    private const val MAX_UNPACKED_BYTES = 512L * 1024 * 1024
    private const val MAX_ENTRIES = 10_000
    private const val MAX_ENTRY_NAME_CHARS = 512
    private const val MAX_BUNDLE_PACKS = 128
    private const val SOURCE_HASH_FILE = ".rdmod-source.sha256"

    private val VALID_SLUG = Regex("[A-Za-z0-9][A-Za-z0-9._-]{0,63}")
    private val VALID_SONAME = Regex("libplugin_[A-Za-z0-9._-]+\\.so")

    data class PackInfo(val slug: String, val title: String)
    data class InstalledPack(val slug: String, val dir: File, val sizeBytes: Long)

    fun modulesDir(activity: Activity): File =
        File(activity.getExternalFilesDir(null), "Modules").apply { mkdirs() }

    private fun installedDir(activity: Activity): File =
        File(activity.filesDir, "user/plugins").apply { mkdirs() }

    fun loadUserPlugins(activity: Activity): Int {
        runCatching { writeReadme(activity) }
            .onFailure { jlog("could not write Modules/README.txt: ${it.message}") }

        importNewPacks(activity)

        var loaded = 0
        installedDir(activity).listFiles { f ->
            f.isDirectory &&
                !f.name.endsWith(".update.tmp") &&
                !f.name.endsWith(".backup")
        }?.forEach { dir ->
            if (loadInstalled(activity, dir)) loaded++
        }

        if (loaded > 0) {
            activity.runOnUiThread {
                Toast.makeText(
                    activity,
                    activity.getString(R.string.modules_loaded, loaded),
                    Toast.LENGTH_SHORT
                ).show()
            }
        }
        return loaded
    }

    fun installedPacks(activity: Activity): List<InstalledPack> =
        installedDir(activity).listFiles { f ->
            f.isDirectory &&
                isValidSlug(f.name) &&
                !f.name.endsWith(".update.tmp") &&
                !f.name.endsWith(".backup")
        }?.map { InstalledPack(it.name, it, dirSize(it)) }
            ?.sortedBy { it.slug.lowercase() }
            ?: emptyList()

    fun uninstall(activity: Activity, slug: String): Boolean {
        if (!isValidSlug(slug)) {
            jlog("refusing to uninstall invalid module slug")
            return false
        }

        var removed = destinationForSlug(activity, slug).deleteRecursively()

        modulesDir(activity).listFiles { f ->
            f.isFile && (f.name.endsWith(".rdmod", true) || f.name.endsWith(".zip", true))
        }?.forEach { pack ->
            if (runCatching { peekPackInfo(pack)?.slug }.getOrNull() == slug) {
                if (pack.delete()) removed = true
            }
        }

        File(installedDir(activity), "$slug.update.tmp").deleteRecursively()
        File(installedDir(activity), "$slug.backup").deleteRecursively()

        jlog("uninstalled module pack $slug (removed=$removed)")
        return removed
    }

    private fun dirSize(dir: File): Long =
        dir.walkTopDown().filter { it.isFile }.map { it.length() }.sum()

    /**
     * Expand all_rdmods.zip-style bundles into individual .rdmod files.
     */
    private fun expandBundles(activity: Activity) {
        val dir = modulesDir(activity)
        val root = dir.canonicalFile
        val bundles = dir.listFiles { f ->
            f.isFile && (f.name.endsWith(".rdmod", true) || f.name.endsWith(".zip", true))
        } ?: return

        for (bundle in bundles) {
            try {
                var written = 0
                var packs = 0
                var totalBytes = 0L
                var isPack = false

                java.util.zip.ZipFile(bundle).use { zf ->
                    if (zf.getEntry("plugin.json") != null) {
                        isPack = true
                        return@use
                    }

                    for (e in zf.entries()) {
                        val name = e.name
                        if (e.isDirectory ||
                            !name.endsWith(".rdmod", ignoreCase = true) ||
                            name.contains('/') ||
                            name.contains('\\')
                        ) continue

                        validateEntryName(name)
                        if (++packs > MAX_BUNDLE_PACKS)
                            throw IllegalArgumentException("too many packs in bundle")

                        val dest = uniquePackDestination(root, name)
                        val remaining = MAX_UNPACKED_BYTES - totalBytes
                        if (remaining <= 0)
                            throw IllegalArgumentException("bundle is too large")

                        val tmp = File(root, ".${dest.name}.part")
                        tmp.delete()

                        try {
                            zf.getInputStream(e).use { input ->
                                tmp.outputStream().use {
                                    totalBytes += copyLimited(
                                        input,
                                        it,
                                        minOf(MAX_PACK_BYTES, remaining)
                                    )
                                }
                            }
                            if (!tmp.renameTo(dest))
                                throw IllegalStateException("could not finalize bundled pack")
                            written++
                        } finally {
                            if (tmp.exists()) tmp.delete()
                        }
                    }
                }

                if (written > 0) {
                    jlog("expanded ${bundle.name} into $written module pack(s)")
                    if (!bundle.delete())
                        jlog("could not remove ${bundle.name} after expanding it")
                } else if (!isPack && packs == 0) {
                    jlog("${bundle.name} is neither a pack nor a bundle of packs")
                }
            } catch (t: Throwable) {
                jlog("bundle ${bundle.name} could not be expanded: ${t.message}")
            }
        }
    }

    /**
     * Import/install/update every loose pack in Modules/.
     *
     * For an existing slug:
     *   1. unpack + validate to <slug>.update.tmp
     *   2. compare with installed copy; identical packs are skipped
     *   3. rename old install to <slug>.backup
     *   4. rename staged copy into the normal path
     *   5. delete backup only after the swap succeeds
     *
     * If step 4 fails, the previous version is restored.
     */
    private fun importNewPacks(activity: Activity) {
        expandBundles(activity)
        recoverInterruptedUpdates(activity)

        val dir = modulesDir(activity)
        val packs = dir.listFiles { f ->
            f.isFile && (f.name.endsWith(".rdmod", true) || f.name.endsWith(".zip", true))
        }

        if (packs == null) {
            jlog("cannot read module drop folder $dir - no packs imported")
            return
        }

        // Newest first. If several source files with the same slug exist,
        // the newly selected/downloaded pack wins and successful import removes
        // older duplicates.
        val ordered = packs.sortedByDescending { it.lastModified() }
        val handledSlugs = HashSet<String>()

        for (pack in ordered) {
            var info: PackInfo? = null
            var staged: File? = null
            var backup: File? = null
            var oldMoved = false

            try {
                info = peekPackInfo(pack) ?: continue
                if (!handledSlugs.add(info.slug)) continue

                val slug = info.slug
                val title = info.title
                val dest = destinationForSlug(activity, slug)
                val sourceHash = sha256(pack)

                // Fast path for a pack already imported by this updater.
                if (dest.isDirectory) {
                    val marker = File(dest, SOURCE_HASH_FILE)
                    if (marker.isFile && marker.readText().trim() == sourceHash) {
                        cleanupDuplicateSources(activity, slug, pack)
                        continue
                    }
                }

                staged = File(installedDir(activity), "$slug.update.tmp")
                backup = File(installedDir(activity), "$slug.backup")
                staged.deleteRecursively()
                backup.deleteRecursively()

                unzip(pack, staged, slug)

                // Existing installs made by older RackDroid builds have no hash
                // marker. Compare their actual extracted contents once so merely
                // upgrading RackDroid does not generate a fake module update.
                if (dest.isDirectory && samePackContents(dest, staged)) {
                    File(dest, SOURCE_HASH_FILE).writeText(sourceHash)
                    staged.deleteRecursively()
                    cleanupDuplicateSources(activity, slug, pack)
                    continue
                }

                File(staged, SOURCE_HASH_FILE).writeText(sourceHash)

                if (!dest.exists()) {
                    if (!staged.renameTo(dest))
                        throw IllegalStateException("could not finalize extracted pack")

                    jlog("installed module pack $slug from ${pack.name}")
                    cleanupDuplicateSources(activity, slug, pack)
                    continue
                }

                // Transactional replacement.
                if (!dest.renameTo(backup))
                    throw IllegalStateException("could not preserve previous version")
                oldMoved = true

                if (!staged.renameTo(dest)) {
                    val restored = backup.renameTo(dest)
                    oldMoved = false
                    if (!restored) {
                        throw IllegalStateException(
                            "could not replace existing files and could not restore previous version"
                        )
                    }
                    throw IllegalStateException("could not replace existing files")
                }

                oldMoved = false
                if (!backup.deleteRecursively())
                    jlog("updated $slug but could not delete backup directory")

                cleanupDuplicateSources(activity, slug, pack)
                jlog("updated module pack $slug from ${pack.name}")

                notifyLong(
                    activity,
                    "$title updated successfully. Restart RackDroid to load the new version."
                )
            } catch (t: Throwable) {
                val title = info?.title ?: pack.nameWithoutExtension

                // Best-effort rollback if the old install was moved but the new
                // one never became the normal destination.
                if (oldMoved && info != null && backup != null) {
                    val dest = runCatching {
                        destinationForSlug(activity, info.slug)
                    }.getOrNull()

                    if (dest != null && !dest.exists() && backup.exists()) {
                        if (backup.renameTo(dest)) {
                            oldMoved = false
                        }
                    }
                }

                staged?.deleteRecursively()

                val kept = if (info != null) {
                    runCatching { destinationForSlug(activity, info.slug).isDirectory }
                        .getOrDefault(false)
                } else false

                val suffix = if (kept)
                    " Previous version was kept."
                else
                    ""

                val reason = t.message ?: "unknown error"
                jlog("module pack ${pack.name} import/update failed: $reason")
                notifyLong(activity, "$title update failed: $reason.$suffix")
            } finally {
                if (oldMoved && info != null && backup != null) {
                    val dest = runCatching {
                        destinationForSlug(activity, info.slug)
                    }.getOrNull()
                    if (dest != null && !dest.exists() && backup.exists())
                        backup.renameTo(dest)
                }
            }
        }
    }

    /**
     * If Android/process death occurred between the two renames, restore the
     * backup before scanning packs. This prevents an interrupted update from
     * leaving a plugin absent on the next launch.
     */
    private fun recoverInterruptedUpdates(activity: Activity) {
        val root = installedDir(activity)

        root.listFiles { f -> f.isDirectory && f.name.endsWith(".backup") }
            ?.forEach { backup ->
                val slug = backup.name.removeSuffix(".backup")
                if (!isValidSlug(slug)) {
                    backup.deleteRecursively()
                    return@forEach
                }
                val dest = destinationForSlug(activity, slug)
                if (!dest.exists()) {
                    if (backup.renameTo(dest))
                        jlog("restored interrupted module update for $slug")
                } else {
                    backup.deleteRecursively()
                }
            }

        root.listFiles { f -> f.isDirectory && f.name.endsWith(".update.tmp") }
            ?.forEach { it.deleteRecursively() }
    }

    /**
     * Remove older source copies of the same slug from Modules/.
     * Keep the exact file that was just accepted so future startup can compare
     * its hash and skip it without doing any replacement work.
     */
    private fun cleanupDuplicateSources(activity: Activity, slug: String, keep: File) {
        modulesDir(activity).listFiles { f ->
            f.isFile &&
                f != keep &&
                (f.name.endsWith(".rdmod", true) || f.name.endsWith(".zip", true))
        }?.forEach { candidate ->
            val candidateSlug = runCatching { peekPackInfo(candidate)?.slug }.getOrNull()
            if (candidateSlug == slug) {
                if (!candidate.delete())
                    jlog("could not remove older source pack ${candidate.name}")
            }
        }
    }

    private fun loadInstalled(activity: Activity, dir: File): Boolean {
        if (!isValidSlug(dir.name)) return false

        val so = try {
            validateInstalledLayout(dir, dir.name)
        } catch (t: Throwable) {
            jlog("installed pack ${dir.name} is invalid: ${t.message}")
            return false
        }

        if ((activity as MainActivity).isPluginLoadedNative(dir.name)) return false

        runCatching { so.setReadOnly() }

        return try {
            System.load(so.absolutePath)
            (activity as MainActivity).loadUserPluginNative(dir.absolutePath, so.name)
        } catch (t: Throwable) {
            jlog("load ${dir.name}/${so.name} failed: ${t.message}")
            false
        }
    }

    private fun peekPackInfo(pack: File): PackInfo? {
        if (pack.length() !in 1..MAX_PACK_BYTES)
            throw IllegalArgumentException(
                "pack size is invalid or exceeds ${MAX_PACK_BYTES / 1048576} MB"
            )

        ZipInputStream(pack.inputStream()).use { zin ->
            var e = zin.nextEntry
            var manifestSeen = false

            while (e != null) {
                if (e.name == "plugin.json") {
                    if (manifestSeen)
                        throw IllegalArgumentException("duplicate plugin.json")
                    manifestSeen = true

                    val jsonText = readEntryBytes(
                        zin,
                        MAX_MANIFEST_BYTES
                    ).toString(Charsets.UTF_8)
                    val json = JSONObject(jsonText)
                    val slug = json.optString("slug")

                    if (!isValidSlug(slug))
                        throw IllegalArgumentException("invalid plugin slug")

                    val title = listOf(
                        json.optString("name"),
                        json.optString("brand"),
                        slug
                    ).firstOrNull { it.isNotBlank() } ?: slug

                    return PackInfo(slug, title)
                }
                e = zin.nextEntry
            }
        }
        return null
    }

    private fun unzip(pack: File, dest: File, expectedSlug: String) {
        dest.mkdirs()
        val root = dest.canonicalFile
        val seenPaths = HashSet<String>()
        var entries = 0
        var totalBytes = 0L

        try {
            ZipInputStream(pack.inputStream()).use { zin ->
                var e = zin.nextEntry
                while (e != null) {
                    entries++
                    if (entries > MAX_ENTRIES)
                        throw IllegalArgumentException("too many archive entries")

                    validateEntryName(e.name)
                    val out = File(root, e.name).canonicalFile

                    if (!out.path.startsWith(root.path + File.separator))
                        throw IllegalArgumentException("archive entry escapes destination")

                    if (!seenPaths.add(out.path))
                        throw IllegalArgumentException("duplicate archive entry")

                    if (e.isDirectory) {
                        out.mkdirs()
                    } else {
                        out.parentFile?.mkdirs()
                        val remaining = MAX_UNPACKED_BYTES - totalBytes
                        if (remaining <= 0)
                            throw IllegalArgumentException("archive is too large")

                        val limit = minOf(MAX_ENTRY_BYTES, remaining)
                        out.outputStream().use {
                            totalBytes += copyLimited(zin, it, limit)
                        }
                    }

                    e = zin.nextEntry
                }
            }

            validateInstalledLayout(root, expectedSlug)
        } catch (t: Throwable) {
            dest.deleteRecursively()
            throw t
        }
    }

    private fun validateInstalledLayout(dir: File, expectedSlug: String): File {
        val root = dir.canonicalFile
        val manifest = File(root, "plugin.json")

        if (!manifest.isFile || manifest.length() !in 1..MAX_MANIFEST_BYTES)
            throw IllegalArgumentException("missing or oversized plugin.json")

        val slug = manifest.inputStream().use {
            JSONObject(
                readEntryBytes(it, MAX_MANIFEST_BYTES).toString(Charsets.UTF_8)
            ).optString("slug")
        }

        if (slug != expectedSlug || !isValidSlug(slug))
            throw IllegalArgumentException("plugin slug does not match install directory")

        val libraries = root.walkTopDown()
            .filter { it.isFile && it.name.endsWith(".so") }
            .toList()

        if (libraries.size != 1)
            throw IllegalArgumentException("pack must contain exactly one native library")

        val so = libraries.single().canonicalFile

        if (so.parentFile != root || !VALID_SONAME.matches(so.name))
            throw IllegalArgumentException(
                "native library must be a root libplugin_*.so file"
            )

        validateElfForDevice(so)
        return so
    }

    private fun validateElfForDevice(so: File) {
        val header = ByteArray(20)

        RandomAccessFile(so, "r").use { file ->
            if (file.length() < header.size)
                throw IllegalArgumentException("native library is truncated")
            file.readFully(header)
        }

        if (header[0] != 0x7f.toByte() ||
            header[1] != 'E'.code.toByte() ||
            header[2] != 'L'.code.toByte() ||
            header[3] != 'F'.code.toByte() ||
            header[4] != 2.toByte() ||
            header[5] != 1.toByte()
        ) {
            throw IllegalArgumentException(
                "native library is not a 64-bit little-endian ELF"
            )
        }

        val machine =
            (header[18].toInt() and 0xff) or
                ((header[19].toInt() and 0xff) shl 8)

        val supportedMachines = Build.SUPPORTED_64_BIT_ABIS.mapNotNull {
            when (it) {
                "arm64-v8a" -> 183
                "x86_64" -> 62
                else -> null
            }
        }.toSet()

        if (machine !in supportedMachines)
            throw IllegalArgumentException(
                "native library ABI is not supported by this device"
            )
    }

    private fun destinationForSlug(activity: Activity, slug: String): File {
        if (!isValidSlug(slug))
            throw IllegalArgumentException("invalid plugin slug")

        val root = installedDir(activity).canonicalFile
        val dest = File(root, slug).canonicalFile

        if (dest.parentFile != root)
            throw IllegalArgumentException("plugin path escapes install directory")

        return dest
    }

    private fun isValidSlug(slug: String): Boolean =
        VALID_SLUG.matches(slug) && slug != "." && slug != ".."

    private fun validateEntryName(name: String) {
        if (name.isBlank() ||
            name.length > MAX_ENTRY_NAME_CHARS ||
            name.indexOf('\u0000') >= 0 ||
            name.startsWith('/') ||
            name.startsWith('\\') ||
            name.contains('\\')
        ) {
            throw IllegalArgumentException("invalid archive entry name")
        }

        val path = name.trimEnd('/')
        if (path.isEmpty() ||
            path.split('/').any { it.isEmpty() || it == "." || it == ".." }
        ) {
            throw IllegalArgumentException("invalid archive entry path")
        }
    }

    private fun readEntryBytes(
        input: java.io.InputStream,
        limit: Long
    ): ByteArray {
        val output = ByteArrayOutputStream()
        copyLimited(input, output, limit)
        return output.toByteArray()
    }

    private fun copyLimited(
        input: java.io.InputStream,
        output: OutputStream,
        limit: Long
    ): Long {
        val buffer = ByteArray(64 * 1024)
        var total = 0L

        while (true) {
            val count = input.read(buffer)
            if (count < 0) break

            total += count
            if (total > limit)
                throw IllegalArgumentException("archive entry exceeds size limit")

            output.write(buffer, 0, count)
        }

        return total
    }

    private fun sha256(file: File): String {
        val md = MessageDigest.getInstance("SHA-256")
        file.inputStream().use { input ->
            val buffer = ByteArray(64 * 1024)
            while (true) {
                val n = input.read(buffer)
                if (n < 0) break
                md.update(buffer, 0, n)
            }
        }
        return md.digest().joinToString("") { "%02x".format(it) }
    }

    /**
     * Full extracted-tree comparison used only for old installs that predate
     * SOURCE_HASH_FILE. New installs use the much cheaper source hash marker.
     */
    private fun samePackContents(a: File, b: File): Boolean =
        directoryDigest(a) == directoryDigest(b)

    private fun directoryDigest(root: File): String {
        val md = MessageDigest.getInstance("SHA-256")
        val canonicalRoot = root.canonicalFile

        val files = canonicalRoot.walkTopDown()
            .filter { it.isFile && it.name != SOURCE_HASH_FILE }
            .map {
                val rel = it.canonicalFile.relativeTo(canonicalRoot).invariantSeparatorsPath
                rel to it
            }
            .sortedBy { it.first }
            .toList()

        val buffer = ByteArray(64 * 1024)

        for ((rel, file) in files) {
            md.update(rel.toByteArray(Charsets.UTF_8))
            md.update(0.toByte())
            file.inputStream().use { input ->
                while (true) {
                    val n = input.read(buffer)
                    if (n < 0) break
                    md.update(buffer, 0, n)
                }
            }
            md.update(0.toByte())
        }

        return md.digest().joinToString("") { "%02x".format(it) }
    }

    private fun uniquePackDestination(root: File, requestedName: String): File {
        val safeRoot = root.canonicalFile
        val dot = requestedName.lastIndexOf('.')
        val stem = if (dot > 0) requestedName.substring(0, dot) else requestedName
        val ext = if (dot > 0) requestedName.substring(dot) else ""

        for (i in 0..9999) {
            val name = if (i == 0) requestedName else "$stem ($i)$ext"
            val candidate = File(safeRoot, name).canonicalFile

            if (candidate.parentFile != safeRoot)
                throw SecurityException("bundle path escapes module folder")

            if (!candidate.exists()) return candidate
        }

        throw IllegalStateException("too many module packs with the same name")
    }

    private fun notifyLong(activity: Activity, text: String) {
        activity.runOnUiThread {
            Toast.makeText(activity, text, Toast.LENGTH_LONG).show()
        }
    }

    private fun writeReadme(activity: Activity) {
        val readme = File(modulesDir(activity), "README.txt")
        if (readme.exists()) return

        readme.writeText(
            "RackDroid — module packs\n\n" +
                "Drop .rdmod pack files here to add or update extra modules, then restart " +
                "RackDroid after an update.\n" +
                "A .rdmod is a zip built for this RackDroid version containing plugin.json, " +
                "res/ and its libplugin_*.so.\n\n" +
                "Installing a pack whose plugin slug already exists safely replaces the " +
                "installed copy after the new pack validates. If replacement fails, the " +
                "previous version is restored.\n\n" +
                "A zip holding several .rdmod files, such as all_rdmods.zip from a release, " +
                "can be dropped here as-is; RackDroid expands and imports the packs.\n"
        )
    }

    private fun jlog(msg: String) =
        android.util.Log.i("rackdroid.modules", msg)
}
