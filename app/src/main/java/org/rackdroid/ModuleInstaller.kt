package org.rackdroid

import android.app.Activity
import android.os.Build
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.OutputStream
import java.io.RandomAccessFile
import java.util.zip.ZipInputStream
import org.json.JSONObject

/** On-demand / side-loadable module packs.
 *
 * WHY this exists: the base APK ships a curated core; extra module packs can
 * be added without rebuilding the app. A pack is a ".rdmod" file (a plain
 * zip) built against this RackDroid engine, containing at its top level:
 *   plugin.json, res/… , libplugin_<name>.so
 *
 * WHERE the user drops them: the app's own external folder
 *   Android/data/org.rackdroid/files/Modules/
 * (user-visible in any file manager, no storage permission needed).
 *
 * HOW they load: a plugin's native library can't be dlopen'd straight from
 * shared storage on modern Android (linker namespace + W^X). So on startup
 * we extract each pack into app-PRIVATE storage (filesDir/user/plugins/<slug>),
 * call Java System.load() on its .so — the one path that satisfies the app
 * classloader's linker namespace — then hand the already-loaded library to
 * the native side (nativeLoadUserPlugin → dlopen RTLD_NOLOAD → register).
 *
 * NOTE for Play: distributing native code that executes from outside Google
 * Play violates Play policy, so this feature is for the sideload/GitHub
 * build. The Play build should deliver extra packs via Play asset packs. */
object ModuleInstaller {
	private const val MAX_PACK_BYTES = 256L * 1024 * 1024
	private const val MAX_MANIFEST_BYTES = 1024L * 1024
	private const val MAX_ENTRY_BYTES = 128L * 1024 * 1024
	private const val MAX_UNPACKED_BYTES = 512L * 1024 * 1024
	private const val MAX_ENTRIES = 10_000
	private const val MAX_ENTRY_NAME_CHARS = 512
	private val VALID_SLUG = Regex("[A-Za-z0-9][A-Za-z0-9._-]{0,63}")
	private val VALID_SONAME = Regex("libplugin_[A-Za-z0-9._-]+\\.so")

	/** Public, user-visible drop folder. Created with a README on first run. */
	fun modulesDir(activity: Activity): File =
		File(activity.getExternalFilesDir(null), "Modules").apply { mkdirs() }

	private fun installedDir(activity: Activity): File =
		File(activity.filesDir, "user/plugins").apply { mkdirs() }

	/** Import any new packs from the drop folder, then load every installed
	 * pack. Call once at startup, after the native engine has initialised
	 * and before the module list JSON is built. */
	fun loadUserPlugins(activity: Activity): Int {
		// The README is a convenience for the user, never a prerequisite. The
		// drop folder can end up owned by another uid (created by adb, a file
		// manager, a backup tool), which makes this write throw -- that must
		// not abort the import/load below, which is the whole point of this
		// call and does not need to write there at all.
		runCatching { writeReadme(activity) }
			.onFailure { jlog("could not write Modules/README.txt: ${it.message}") }
		importNewPacks(activity)
		var loaded = 0
		installedDir(activity).listFiles { f -> f.isDirectory }?.forEach { dir ->
			if (loadInstalled(activity, dir)) loaded++
		}
		if (loaded > 0)
			activity.runOnUiThread {
				android.widget.Toast.makeText(activity,
					activity.getString(R.string.modules_loaded, loaded),
					android.widget.Toast.LENGTH_SHORT).show()
			}
		return loaded
	}

	/** An installed pack: its plugin slug (== the extracted dir name) and the
	 * on-disk size of its extracted copy, for the manager UI. */
	data class InstalledPack(val slug: String, val dir: File, val sizeBytes: Long)

	/** Every pack currently extracted into private storage (i.e. loaded at the
	 * last startup, or just installed live). Sorted by slug. */
	fun installedPacks(activity: Activity): List<InstalledPack> =
		installedDir(activity).listFiles { f -> f.isDirectory && isValidSlug(f.name) }
			?.map { InstalledPack(it.name, it, dirSize(it)) }
			?.sortedBy { it.slug.lowercase() }
			?: emptyList()

	/** Uninstall a pack: remove its extracted copy AND the source .rdmod in the
	 * drop folder (so it does not re-import next launch). The native library is
	 * already loaded into the running engine and cannot be safely unregistered
	 * mid-session, so the module only fully disappears after a restart -- the
	 * caller tells the user. Returns true if anything was removed. */
	fun uninstall(activity: Activity, slug: String): Boolean {
		if (!isValidSlug(slug)) {
			jlog("refusing to uninstall invalid module slug")
			return false
		}
		var removed = destinationForSlug(activity, slug).deleteRecursively()
		// Drop any source pack in Modules/ whose plugin.json slug matches.
		modulesDir(activity).listFiles { f ->
			f.isFile && (f.name.endsWith(".rdmod") || f.name.endsWith(".zip"))
		}?.forEach { pack ->
			if (runCatching { peekSlug(pack) }.getOrNull() == slug)
				if (pack.delete()) removed = true
		}
		jlog("uninstalled module pack $slug (removed=$removed)")
		return removed
	}

	private fun dirSize(dir: File): Long =
		dir.walkTopDown().filter { it.isFile }.map { it.length() }.sum()

	private fun importNewPacks(activity: Activity) {
		val dir = modulesDir(activity)
		// listFiles() returns null when the folder is missing or unreadable
		// (again: possible when another uid created it). Silently treating
		// that as "no packs to import" hides the one failure the user would
		// actually need to act on, so say it out loud.
		val packs = dir.listFiles { f ->
			f.isFile && (f.name.endsWith(".rdmod") || f.name.endsWith(".zip"))
		}
		if (packs == null) {
			jlog("cannot read module drop folder $dir - no packs imported")
			return
		}
		for (pack in packs) {
			var tmp: File? = null
			try {
				val slug = peekSlug(pack) ?: continue
				val dest = destinationForSlug(activity, slug)
				if (dest.exists()) continue // already installed; drop a new slug to add
				tmp = File(dest.parentFile, "$slug.tmp")
				tmp.deleteRecursively()
				unzip(pack, tmp, slug)
				if (!tmp.renameTo(dest))
					throw IllegalStateException("could not finalize extracted pack")
				jlog("installed module pack $slug from ${pack.name}")
			} catch (t: Throwable) {
				jlog("module pack ${pack.name} import failed: ${t.message}")
				tmp?.deleteRecursively()
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
		// Re-imports rescan every installed pack: skip the ones already
		// registered (dir name == plugin slug) instead of paying a redundant
		// System.load + dlopen that ends in a WARN on the native side.
		if ((activity as MainActivity).isPluginLoadedNative(dir.name)) return false
		// Android refuses to load a writable .so on newer versions ("Attempt
		// to load writable file … will throw"); make it read-only first.
		runCatching { so.setReadOnly() }
		return try {
			System.load(so.absolutePath) // must precede the native dlopen
			(activity as MainActivity).loadUserPluginNative(dir.absolutePath, so.name)
		} catch (t: Throwable) {
			jlog("load ${dir.name}/${so.name} failed: ${t.message}")
			false
		}
	}

	private fun peekSlug(pack: File): String? {
		if (pack.length() !in 1..MAX_PACK_BYTES)
			throw IllegalArgumentException("pack size is invalid or exceeds ${MAX_PACK_BYTES / 1048576} MB")
		ZipInputStream(pack.inputStream()).use { zin ->
			var e = zin.nextEntry
			var manifestSeen = false
			while (e != null) {
				if (e.name == "plugin.json") {
					if (manifestSeen) throw IllegalArgumentException("duplicate plugin.json")
					manifestSeen = true
					val json = readEntryBytes(zin, MAX_MANIFEST_BYTES).toString(Charsets.UTF_8)
					val slug = JSONObject(json).optString("slug")
					if (!isValidSlug(slug))
						throw IllegalArgumentException("invalid plugin slug")
					return slug
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
		ZipInputStream(pack.inputStream()).use { zin ->
			var e = zin.nextEntry
			while (e != null) {
				entries++
				if (entries > MAX_ENTRIES) throw IllegalArgumentException("too many archive entries")
				validateEntryName(e.name)
				val out = File(root, e.name).canonicalFile
				// Zip-slip guard. Invalid entries reject the entire native-code pack.
				if (!out.path.startsWith(root.path + File.separator))
					throw IllegalArgumentException("archive entry escapes destination")
				if (!seenPaths.add(out.path))
					throw IllegalArgumentException("duplicate archive entry")
				if (e.isDirectory) out.mkdirs()
				else {
					out.parentFile?.mkdirs()
					val remaining = MAX_UNPACKED_BYTES - totalBytes
					if (remaining <= 0) throw IllegalArgumentException("archive is too large")
					val limit = minOf(MAX_ENTRY_BYTES, remaining)
					out.outputStream().use { totalBytes += copyLimited(zin, it, limit) }
				}
				e = zin.nextEntry
			}
		}
		validateInstalledLayout(root, expectedSlug)
	}

	private fun validateInstalledLayout(dir: File, expectedSlug: String): File {
		val root = dir.canonicalFile
		val manifest = File(root, "plugin.json")
		if (!manifest.isFile || manifest.length() !in 1..MAX_MANIFEST_BYTES)
			throw IllegalArgumentException("missing or oversized plugin.json")
		val slug = manifest.inputStream().use {
			JSONObject(readEntryBytes(it, MAX_MANIFEST_BYTES).toString(Charsets.UTF_8)).optString("slug")
		}
		if (slug != expectedSlug || !isValidSlug(slug))
			throw IllegalArgumentException("plugin slug does not match install directory")
		val libraries = root.walkTopDown().filter { it.isFile && it.name.endsWith(".so") }.toList()
		if (libraries.size != 1)
			throw IllegalArgumentException("pack must contain exactly one native library")
		val so = libraries.single().canonicalFile
		if (so.parentFile != root || !VALID_SONAME.matches(so.name))
			throw IllegalArgumentException("native library must be a root libplugin_*.so file")
		validateElfForDevice(so)
		return so
	}

	private fun validateElfForDevice(so: File) {
		val header = ByteArray(20)
		RandomAccessFile(so, "r").use { file ->
			if (file.length() < header.size) throw IllegalArgumentException("native library is truncated")
			file.readFully(header)
		}
		if (header[0] != 0x7f.toByte() || header[1] != 'E'.code.toByte() ||
			header[2] != 'L'.code.toByte() || header[3] != 'F'.code.toByte() ||
			header[4] != 2.toByte() || header[5] != 1.toByte())
			throw IllegalArgumentException("native library is not a 64-bit little-endian ELF")
		val machine = (header[18].toInt() and 0xff) or ((header[19].toInt() and 0xff) shl 8)
		val supportedMachines = Build.SUPPORTED_64_BIT_ABIS.mapNotNull {
			when (it) {
				"arm64-v8a" -> 183 // EM_AARCH64
				"x86_64" -> 62 // EM_X86_64
				else -> null
			}
		}.toSet()
		if (machine !in supportedMachines)
			throw IllegalArgumentException("native library ABI is not supported by this device")
	}

	private fun destinationForSlug(activity: Activity, slug: String): File {
		if (!isValidSlug(slug)) throw IllegalArgumentException("invalid plugin slug")
		val root = installedDir(activity).canonicalFile
		val dest = File(root, slug).canonicalFile
		if (dest.parentFile != root) throw IllegalArgumentException("plugin path escapes install directory")
		return dest
	}

	private fun isValidSlug(slug: String): Boolean =
		VALID_SLUG.matches(slug) && slug != "." && slug != ".."

	private fun validateEntryName(name: String) {
		if (name.isBlank() || name.length > MAX_ENTRY_NAME_CHARS || name.indexOf('\u0000') >= 0 ||
			name.startsWith('/') || name.startsWith('\\') || name.contains('\\'))
			throw IllegalArgumentException("invalid archive entry name")
		val path = name.trimEnd('/')
		if (path.isEmpty() || path.split('/').any { it.isEmpty() || it == "." || it == ".." })
			throw IllegalArgumentException("invalid archive entry path")
	}

	private fun readEntryBytes(input: java.io.InputStream, limit: Long): ByteArray {
		val output = ByteArrayOutputStream()
		copyLimited(input, output, limit)
		return output.toByteArray()
	}

	private fun copyLimited(input: java.io.InputStream, output: OutputStream, limit: Long): Long {
		val buffer = ByteArray(64 * 1024)
		var total = 0L
		while (true) {
			val count = input.read(buffer)
			if (count < 0) break
			total += count
			if (total > limit) throw IllegalArgumentException("archive entry exceeds size limit")
			output.write(buffer, 0, count)
		}
		return total
	}

	private fun writeReadme(activity: Activity) {
		val readme = File(modulesDir(activity), "README.txt")
		if (readme.exists()) return
		readme.writeText(
			"RackDroid — module packs\n\n" +
			"Drop .rdmod pack files here to add extra modules, then restart the app.\n" +
			"A .rdmod is a zip built for this RackDroid version containing plugin.json, " +
			"res/ and its libplugin_*.so.\n")
	}

	private fun jlog(msg: String) = android.util.Log.i("rackdroid.modules", msg)
}
