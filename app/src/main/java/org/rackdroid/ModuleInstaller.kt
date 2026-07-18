package org.rackdroid

import android.app.Activity
import java.io.File
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

	/** Public, user-visible drop folder. Created with a README on first run. */
	fun modulesDir(activity: Activity): File =
		File(activity.getExternalFilesDir(null), "Modules").apply { mkdirs() }

	private fun installedDir(activity: Activity): File =
		File(activity.filesDir, "user/plugins").apply { mkdirs() }

	/** Import any new packs from the drop folder, then load every installed
	 * pack. Call once at startup, after the native engine has initialised
	 * and before the module list JSON is built. */
	fun loadUserPlugins(activity: Activity): Int {
		writeReadme(activity)
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
		installedDir(activity).listFiles { f -> f.isDirectory }
			?.map { InstalledPack(it.name, it, dirSize(it)) }
			?.sortedBy { it.slug.lowercase() }
			?: emptyList()

	/** Uninstall a pack: remove its extracted copy AND the source .rdmod in the
	 * drop folder (so it does not re-import next launch). The native library is
	 * already loaded into the running engine and cannot be safely unregistered
	 * mid-session, so the module only fully disappears after a restart -- the
	 * caller tells the user. Returns true if anything was removed. */
	fun uninstall(activity: Activity, slug: String): Boolean {
		var removed = File(installedDir(activity), slug).deleteRecursively()
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
		val packs = modulesDir(activity).listFiles { f ->
			f.isFile && (f.name.endsWith(".rdmod") || f.name.endsWith(".zip"))
		} ?: return
		for (pack in packs) {
			try {
				val slug = peekSlug(pack) ?: continue
				val dest = File(installedDir(activity), slug)
				if (dest.exists()) continue // already installed; drop a new slug to add
				val tmp = File(dest.parentFile, "$slug.tmp")
				tmp.deleteRecursively()
				unzip(pack, tmp)
				tmp.renameTo(dest)
				jlog("installed module pack $slug from ${pack.name}")
			} catch (t: Throwable) {
				jlog("module pack ${pack.name} import failed: ${t.message}")
			}
		}
	}

	private fun loadInstalled(activity: Activity, dir: File): Boolean {
		val so = dir.listFiles { f -> f.name.endsWith(".so") }?.firstOrNull() ?: return false
		if (!File(dir, "plugin.json").exists()) return false
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
		ZipInputStream(pack.inputStream()).use { zin ->
			var e = zin.nextEntry
			while (e != null) {
				if (e.name.substringAfterLast('/') == "plugin.json") {
					val json = zin.readBytes().toString(Charsets.UTF_8)
					return JSONObject(json).optString("slug").ifEmpty { null }
				}
				e = zin.nextEntry
			}
		}
		return null
	}

	private fun unzip(pack: File, dest: File) {
		dest.mkdirs()
		ZipInputStream(pack.inputStream()).use { zin ->
			var e = zin.nextEntry
			while (e != null) {
				val out = File(dest, e.name)
				// Zip-slip guard.
				if (!out.canonicalPath.startsWith(dest.canonicalPath + File.separator)) {
					e = zin.nextEntry; continue
				}
				if (e.isDirectory) out.mkdirs()
				else {
					out.parentFile?.mkdirs()
					out.outputStream().use { zin.copyTo(it) }
				}
				e = zin.nextEntry
			}
		}
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
