package org.rackdroid

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.provider.DocumentsContract
import android.provider.OpenableColumns
import java.io.File

/**
 * Minimal helper Activity for Rack patch Save As.
 *
 * VCV Rack's patch code needs a normal POSIX path. Android's Storage Access
 * Framework returns a content:// URI instead. The bridge therefore keeps a
 * private mirror under filesDir/user/patches (which is included in RackDroid's
 * Android backup allow-list) and remembers which SAF document that mirror is
 * linked to. Native patch saving writes the mirror first, then synchronously
 * publishes the finished .vcv to the linked URI.
 */
class DocumentSaveActivity : Activity() {

	private val requestCreateDocument = 1

	override fun onCreate(savedInstanceState: Bundle?) {
		super.onCreate(savedInstanceState)
		if (savedInstanceState != null)
			return

		val requested = intent?.getStringExtra(EXTRA_SAVE_FILENAME)
			?.trim().orEmpty().ifEmpty { "Untitled.vcv" }
		val title = safeVcvName(requested)
		val create = Intent(Intent.ACTION_CREATE_DOCUMENT).apply {
			addCategory(Intent.CATEGORY_OPENABLE)
			type = "application/octet-stream"
			putExtra(Intent.EXTRA_TITLE, title)
			addFlags(
				Intent.FLAG_GRANT_READ_URI_PERMISSION or
					Intent.FLAG_GRANT_WRITE_URI_PERMISSION or
					Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION
			)
			// Put the user back near the last successful Save As location when
			// possible. Passing the prior document URI is supported: DocumentsUI
			// opens the containing folder when the URI points at a file.
			getSharedPreferences(PREFS, Context.MODE_PRIVATE)
				.getString(KEY_LAST_URI, null)
				?.let { runCatching { putExtra(DocumentsContract.EXTRA_INITIAL_URI, Uri.parse(it)) } }
		}

		try {
			startActivityForResult(create, requestCreateDocument)
		} catch (_: Throwable) {
			nativeDocumentSaveResult(null)
			finish()
		}
	}

	@Deprecated("Deprecated in Java")
	override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
		super.onActivityResult(requestCode, resultCode, data)
		if (requestCode != requestCreateDocument)
			return

		val uri = if (resultCode == RESULT_OK) data?.data else null
		if (uri == null) {
			nativeDocumentSaveResult(null)
			finish()
			return
		}

		try {
			// Keep the URI usable across normal app/process restarts. Providers
			// that do not offer persistable grants are still usable for this
			// process; the call is deliberately best-effort.
			val grantFlags = (data?.flags ?: 0) and
				(Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION)
			if (grantFlags != 0) {
				runCatching { contentResolver.takePersistableUriPermission(uri, grantFlags) }
			}

			val display = queryDisplayName(uri) ?: "Untitled.vcv"
			val safeName = safeVcvName(display)
			val patchDir = File(filesDir, "user/patches").canonicalFile.apply { mkdirs() }
			val uriString = uri.toString()
			val prefs = getSharedPreferences(PREFS, Context.MODE_PRIVATE)
			val reverseKey = KEY_URI_PREFIX + uriString
			val remembered = prefs.getString(reverseKey, null) ?: prefs.all.entries
				.firstOrNull { (key, value) -> key != KEY_LAST_URI && !key.startsWith(KEY_URI_PREFIX) &&
					File(key).isAbsolute && value == uriString }
				?.key
			val rememberedFile = remembered?.let { runCatching { File(it).canonicalFile }.getOrNull() }
			val mirror = rememberedFile?.takeIf {
				it.parentFile == patchDir && it.name.endsWith(".vcv", ignoreCase = true)
			} ?: uniqueDestination(patchDir, safeName)

			// Commit this synchronously before waking native save code. If the
			// process dies immediately afterward, the link is still durable.
			val saved = prefs.edit()
				.putString(mirror.absolutePath, uriString)
				.putString(reverseKey, mirror.absolutePath)
				.putString(KEY_LAST_URI, uriString)
				.commit()
			if (!saved)
				throw IllegalStateException("could not remember selected document")

			nativeDocumentSaveResult(mirror.absolutePath)
		} catch (_: Throwable) {
			nativeDocumentSaveResult(null)
		}
		finish()
	}

	private fun queryDisplayName(uri: Uri): String? {
		contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { cursor ->
			val index = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME)
			if (index >= 0 && cursor.moveToFirst())
				return cursor.getString(index)
		}
		return uri.lastPathSegment
	}

	private fun safeVcvName(raw: String): String {
		var name = raw.replace('\\', '/').substringAfterLast('/')
			.filter { it.code >= 32 && it != '/' }
			.replace(Regex("[^A-Za-z0-9._ ()+\\-]"), "_")
			.trim()
			.take(160)
		if (name.isEmpty() || name == "." || name == "..")
			name = "Untitled.vcv"
		if (!name.endsWith(".vcv", ignoreCase = true))
			name += ".vcv"
		return name
	}

	private fun uniqueDestination(dir: File, requestedName: String): File {
		val root = dir.canonicalFile
		val dot = requestedName.lastIndexOf('.')
		val stem = if (dot > 0) requestedName.substring(0, dot) else requestedName
		val ext = if (dot > 0) requestedName.substring(dot) else ""
		for (i in 0..9999) {
			val name = if (i == 0) requestedName else "$stem ($i)$ext"
			val candidate = File(root, name).canonicalFile
			if (candidate.parentFile != root)
				throw SecurityException("save path escapes patch directory")
			if (!candidate.exists())
				return candidate
		}
		throw IllegalStateException("too many patches with the same name")
	}

	private external fun nativeDocumentSaveResult(path: String?)

	companion object {
		const val EXTRA_SAVE_FILENAME = "org.rackdroid.extra.SAVE_FILENAME"
		const val PREFS = "document_save_links"
		const val KEY_LAST_URI = "__last_uri__"
		const val KEY_URI_PREFIX = "__uri__:"

		init {
			// The callback lives in jni_bridge.cpp inside librack_engine.so.
			// MainActivity already loads this in normal use; repeating the call
			// is harmless and makes this helper self-contained.
			System.loadLibrary("rack_engine")
		}
	}
}
