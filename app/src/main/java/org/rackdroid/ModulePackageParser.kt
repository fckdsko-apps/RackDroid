package org.rackdroid

import android.os.Build
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.OutputStream
import java.io.RandomAccessFile
import java.security.MessageDigest
import java.util.zip.ZipFile
import java.util.zip.ZipInputStream
import org.json.JSONObject

object ModulePackageParser {
    const val MAX_PACK_BYTES = 256L * 1024 * 1024
    private const val MAX_MANIFEST_BYTES = 1024L * 1024
    private const val MAX_ENTRY_BYTES = 128L * 1024 * 1024
    private const val MAX_UNPACKED_BYTES = 512L * 1024 * 1024
    private const val MAX_ENTRIES = 10_000
    private const val MAX_ENTRY_NAME_CHARS = 512
    private const val MAX_BUNDLE_PACKS = 128
    private val VALID_SLUG = Regex("[A-Za-z0-9][A-Za-z0-9._-]{0,63}")
    private val VALID_SONAME = Regex("libplugin_[A-Za-z0-9._-]+\\.so")

    fun isValidSlug(slug: String): Boolean =
        VALID_SLUG.matches(slug) && slug != "." && slug != ".."

    fun inspect(pack: File): ModulePackageDescriptor {
        if (!pack.isFile || pack.length() !in 1..MAX_PACK_BYTES)
            throw IllegalArgumentException("pack size is invalid or exceeds ${MAX_PACK_BYTES / 1048576} MB")

        var manifest: JSONObject? = null
        var manifestCount = 0
        ZipInputStream(pack.inputStream()).use { zin ->
            var e = zin.nextEntry
            while (e != null) {
                validateEntryName(e.name)
                if (e.name == "plugin.json") {
                    manifestCount++
                    if (manifestCount > 1) throw IllegalArgumentException("duplicate plugin.json")
                    manifest = JSONObject(readEntryBytes(zin, MAX_MANIFEST_BYTES).toString(Charsets.UTF_8))
                }
                e = zin.nextEntry
            }
        }
        val json = manifest ?: throw IllegalArgumentException("missing plugin.json")
        val slug = json.optString("slug")
        if (!isValidSlug(slug)) throw IllegalArgumentException("invalid plugin slug")
        val title = listOf(json.optString("name"), json.optString("brand"), slug)
            .firstOrNull { it.isNotBlank() } ?: slug
        val version = json.optString("version").takeIf { it.isNotBlank() }

        // Full extraction validation discovers the actual native library and ABI.
        val scratch = File(pack.parentFile, ".inspect-${slug}-${System.nanoTime()}")
        return try {
            val so = extractAndValidate(pack, scratch, slug)
            ModulePackageDescriptor(slug, title, version, sha256(pack), so.name, abiForElf(so), pack)
        } finally {
            scratch.deleteRecursively()
        }
    }

    fun isSinglePack(file: File): Boolean = try {
        ZipFile(file).use { it.getEntry("plugin.json") != null }
    } catch (_: Throwable) { false }

    /** Expand a bundle into private work files. A single .rdmod is returned unchanged. */
    fun expandInput(input: File, workDir: File): List<File> {
        if (isSinglePack(input)) return listOf(input)
        workDir.mkdirs()
        val root = workDir.canonicalFile
        val result = ArrayList<File>()
        var total = 0L
        ZipFile(input).use { zf ->
            val entries = zf.entries()
            while (entries.hasMoreElements()) {
                val e = entries.nextElement()
                val name = e.name
                if (e.isDirectory || !name.endsWith(".rdmod", true) || name.contains('/') || name.contains('\\')) continue
                validateEntryName(name)
                if (result.size >= MAX_BUNDLE_PACKS) throw IllegalArgumentException("too many packs in bundle")
                val out = File(root, String.format("%03d-%s", result.size, name)).canonicalFile
                if (out.parentFile != root) throw IllegalArgumentException("bundle entry escapes work directory")
                zf.getInputStream(e).use { src ->
                    out.outputStream().use { dst -> total += copyLimited(src, dst, MAX_PACK_BYTES) }
                }
                if (total > MAX_UNPACKED_BYTES) throw IllegalArgumentException("bundle is too large")
                result.add(out)
            }
        }
        if (result.isEmpty()) throw IllegalArgumentException("file is neither an .rdmod pack nor a bundle of .rdmod packs")
        return result
    }

    fun extractAndValidate(pack: File, dest: File, expectedSlug: String): File {
        if (dest.exists()) dest.deleteRecursively()
        dest.mkdirs()
        val root = dest.canonicalFile
        val seen = HashSet<String>()
        var count = 0
        var total = 0L
        try {
            ZipInputStream(pack.inputStream()).use { zin ->
                var e = zin.nextEntry
                while (e != null) {
                    if (++count > MAX_ENTRIES) throw IllegalArgumentException("too many archive entries")
                    validateEntryName(e.name)
                    val out = File(root, e.name).canonicalFile
                    if (!out.path.startsWith(root.path + File.separator)) throw IllegalArgumentException("archive entry escapes destination")
                    if (!seen.add(out.path)) throw IllegalArgumentException("duplicate archive entry")
                    if (e.isDirectory) {
                        if (!out.mkdirs() && !out.isDirectory) throw IllegalStateException("cannot create archive directory")
                    } else {
                        if (out.exists() && out.isDirectory) throw IllegalArgumentException("archive file/directory collision")
                        out.parentFile?.let { if (!it.mkdirs() && !it.isDirectory) throw IllegalStateException("cannot create archive directory") }
                        val remaining = MAX_UNPACKED_BYTES - total
                        if (remaining <= 0) throw IllegalArgumentException("archive is too large")
                        out.outputStream().use { total += copyLimited(zin, it, minOf(MAX_ENTRY_BYTES, remaining)) }
                    }
                    e = zin.nextEntry
                }
            }
            return validateInstalledLayout(root, expectedSlug)
        } catch (t: Throwable) {
            dest.deleteRecursively()
            throw t
        }
    }

    fun validateInstalledLayout(dir: File, expectedSlug: String): File {
        val root = dir.canonicalFile
        val manifest = File(root, "plugin.json")
        if (!manifest.isFile || manifest.length() !in 1..MAX_MANIFEST_BYTES)
            throw IllegalArgumentException("missing or oversized plugin.json")
        val json = JSONObject(manifest.readText())
        val slug = json.optString("slug")
        if (slug != expectedSlug || !isValidSlug(slug)) throw IllegalArgumentException("plugin slug does not match install directory")
        val libraries = root.walkTopDown().filter { it.isFile && it.name.endsWith(".so") }.toList()
        if (libraries.size != 1) throw IllegalArgumentException("pack must contain exactly one native library")
        val so = libraries.single().canonicalFile
        if (so.parentFile != root || !VALID_SONAME.matches(so.name))
            throw IllegalArgumentException("native library must be a root libplugin_*.so file")
        validateElfForDevice(so)
        return so
    }

    fun sha256(file: File): String {
        val md = MessageDigest.getInstance("SHA-256")
        file.inputStream().use { input ->
            val buf = ByteArray(64 * 1024)
            while (true) {
                val n = input.read(buf); if (n < 0) break; md.update(buf, 0, n)
            }
        }
        return md.digest().joinToString("") { "%02x".format(it) }
    }

    fun directoryDigest(root: File): String {
        val md = MessageDigest.getInstance("SHA-256")
        val r = root.canonicalFile
        val files = r.walkTopDown().filter {
            it.isFile && it.name != ".rdmod-source.sha256"
        }.map {
            it.canonicalFile.relativeTo(r).invariantSeparatorsPath to it
        }.sortedBy { it.first }.toList()
        val buf = ByteArray(64 * 1024)
        for ((rel, file) in files) {
            md.update(rel.toByteArray(Charsets.UTF_8)); md.update(0.toByte())
            file.inputStream().use { input -> while (true) { val n = input.read(buf); if (n < 0) break; md.update(buf, 0, n) } }
            md.update(0.toByte())
        }
        return md.digest().joinToString("") { "%02x".format(it) }
    }

    private fun abiForElf(so: File): String {
        val header = readElfHeader(so)
        val machine = (header[18].toInt() and 0xff) or ((header[19].toInt() and 0xff) shl 8)
        return when (machine) { 183 -> "arm64-v8a"; 62 -> "x86_64"; else -> "unknown-$machine" }
    }

    private fun validateElfForDevice(so: File) {
        val header = readElfHeader(so)
        if (header[0] != 0x7f.toByte() || header[1] != 'E'.code.toByte() || header[2] != 'L'.code.toByte() ||
            header[3] != 'F'.code.toByte() || header[4] != 2.toByte() || header[5] != 1.toByte())
            throw IllegalArgumentException("native library is not a 64-bit little-endian ELF")
        val machine = (header[18].toInt() and 0xff) or ((header[19].toInt() and 0xff) shl 8)
        val supported = Build.SUPPORTED_64_BIT_ABIS.mapNotNull { when (it) { "arm64-v8a" -> 183; "x86_64" -> 62; else -> null } }.toSet()
        if (machine !in supported) throw IllegalArgumentException("native library ABI is not supported by this device")
    }

    private fun readElfHeader(so: File): ByteArray {
        val h = ByteArray(20)
        RandomAccessFile(so, "r").use { f -> if (f.length() < h.size) throw IllegalArgumentException("native library is truncated"); f.readFully(h) }
        return h
    }

    private fun validateEntryName(name: String) {
        if (name.isBlank() || name.length > MAX_ENTRY_NAME_CHARS || name.indexOf('\u0000') >= 0 ||
            name.startsWith('/') || name.startsWith('\\') || name.contains('\\'))
            throw IllegalArgumentException("invalid archive entry name")
        val path = name.trimEnd('/')
        if (path.isEmpty() || path.split('/').any { it.isEmpty() || it == "." || it == ".." })
            throw IllegalArgumentException("invalid archive entry path")
    }

    private fun readEntryBytes(input: java.io.InputStream, limit: Long): ByteArray {
        val out = ByteArrayOutputStream(); copyLimited(input, out, limit); return out.toByteArray()
    }

    private fun copyLimited(input: java.io.InputStream, output: OutputStream, limit: Long): Long {
        val buf = ByteArray(64 * 1024); var total = 0L
        while (true) { val n = input.read(buf); if (n < 0) break; total += n; if (total > limit) throw IllegalArgumentException("archive entry exceeds size limit"); output.write(buf, 0, n) }
        return total
    }
}
