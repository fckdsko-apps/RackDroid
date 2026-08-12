package org.rackdroid

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.provider.DocumentsContract
import android.provider.OpenableColumns
import android.widget.Toast
import java.io.File
import java.util.UUID

/**
 * System-document picker used specifically for VCV patch Open.
 *
 * Rack needs a normal filesystem path, while Android SAF returns content://.
 * The selected patch is therefore mirrored under filesDir/user/patches, then
 * Rack opens that mirror. When the provider grants persistent write access,
 * the mirror is linked to the original URI so ordinary File > Save publishes
 * back to the exact external document through commitDocumentSave().
 */
class DocumentOpenActivity : Activity() {

	private val requestOpenDocument = 1
	private val maxPatchBytes = 512L * 1024L * 1024L

	override fun onCreate(savedInstanceState: Bundle?) {
		super.onCreate(savedInstanceState)
		if (savedInstanceState != null)
			return

		val open = Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
			addCategory(Intent.CATEGORY_OPENABLE)
			// .vcv has no universally registered MIME type. Restrict by filename
			// after selection instead of hiding valid patches from providers.
			type = "*/*"
			addFlags(
				Intent.FLAG_GRANT_READ_URI_PERMISSION or
					Intent.FLAG_GRANT_WRITE_URI_PERMISSION or
					Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION
			)
			getSharedPreferences(DocumentSaveActivity.PREFS, Context.MODE_PRIVATE)
				.getString(DocumentSaveActivity.KEY_LAST_URI, null)
				?.let {
					runCatching {
						putExtra(DocumentsContract.EXTRA_INITIAL_URI, Uri.parse(it))
					}
				}
		}

		try {
			startActivityForResult(open, requestOpenDocument)
		} catch (_: Throwable) {
			nativeDocumentOpenResult(null)
			finish()
		}
	}

	@Deprecated("Deprecated in Java")
	override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
		super.onActivityResult(requestCode, resultCode, data)
		if (requestCode != requestOpenDocument)
			return

		val uri = if (resultCode == RESULT_OK) data?.data else null
		if (uri == null) {
			nativeDocumentOpenResult(null)
			finish()
			return
		}

		try {
			val display = queryDisplayName(uri) ?: "imported.vcv"
			val normalized = display.replace('\\', '/').substringAfterLast('/')
			val explicitExt = normalized.substringAfterLast('.', "")
			if (explicitExt.isNotEmpty() && !explicitExt.equals("vcv", ignoreCase = true))
				throw IllegalArgumentException("selected file is not a .vcv patch")
			val safeName = safeVcvName(normalized)

			// Persist whatever the provider actually granted. ACTION_OPEN_DOCUMENT
			// is designed for persistable access, but third-party providers may
			// decline write persistence. In that case the patch still opens safely
			// as a detached local mirror and Save As remains available.
			val grantFlags = (data?.flags ?: 0) and
				(Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION)
			if (grantFlags != 0)
				runCatching { contentResolver.takePersistableUriPermission(uri, grantFlags) }

			val prefs = getSharedPreferences(DocumentSaveActivity.PREFS, Context.MODE_PRIVATE)
			val uriString = uri.toString()
			val reverseKey = DocumentSaveActivity.KEY_URI_PREFIX + uriString
			val patchDir = File(filesDir, "user/patches").canonicalFile.apply { mkdirs() }

			// v02 stored path -> URI only. Prefer the new reverse index, but scan
			// once for that older shape so a v02-created external patch reuses its
			// existing mirror instead of creating a second linked copy.
			val remembered = prefs.getString(reverseKey, null) ?: prefs.all.entries
				.firstOrNull { (key, value) ->
					key != DocumentSaveActivity.KEY_LAST_URI &&
					!key.startsWith(DocumentSaveActivity.KEY_URI_PREFIX) &&
					File(key).isAbsolute && value == uriString
				}
				?.key
			val rememberedFile = remembered?.let { runCatching { File(it).canonicalFile }.getOrNull() }
			val reusable = rememberedFile?.takeIf {
				it.parentFile == patchDir && it.name.endsWith(".vcv", ignoreCase = true)
			}
			val mirror = reusable ?: uniqueDestination(patchDir, safeName)

			copyUriReplacingAtomically(uri, mirror, maxPatchBytes)

			val persistentWrite = contentResolver.persistedUriPermissions.any {
				it.uri == uri && it.isWritePermission
			}
			if (persistentWrite) {
				val saved = prefs.edit()
					.putString(mirror.absolutePath, uriString)
					.putString(reverseKey, mirror.absolutePath)
					.putString(DocumentSaveActivity.KEY_LAST_URI, uriString)
					.commit()
				if (!saved)
					throw IllegalStateException("could not remember external patch link")
			} else {
				// Do not pretend ordinary Save will update an external document if
				// that promise cannot survive a process restart.
				prefs.edit()
					.remove(mirror.absolutePath)
					.remove(reverseKey)
					.apply()
				runOnUiThread {
					Toast.makeText(
						this,
						"Opened a local copy. This provider did not grant persistent write access; use Save As to write externally.",
						Toast.LENGTH_LONG
					).show()
				}
			}

			nativeDocumentOpenResult(mirror.absolutePath)
		} catch (e: Exception) {
			runOnUiThread {
				Toast.makeText(
					this,
					"Couldn't open patch: ${e.message ?: "unknown error"}",
					Toast.LENGTH_LONG
				).show()
			}
			nativeDocumentOpenResult(null)
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
			name = "imported.vcv"
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
				throw SecurityException("open path escapes patch directory")
			if (!candidate.exists())
				return candidate
		}
		throw IllegalStateException("too many patches with the same name")
	}

	/**
	 * Imports to a sibling temporary file first, then swaps it into place.
	 * If a previously linked mirror exists, keep a one-operation backup until
	 * the replacement has been finalized so a failed import cannot destroy the
	 * last good local copy.
	 */
	private fun copyUriReplacingAtomically(uri: Uri, destination: File, maxBytes: Long) {
		val parent = destination.parentFile ?: throw IllegalStateException("patch directory is missing")
		parent.mkdirs()
		val tmp = File(parent, ".${destination.name}.${UUID.randomUUID()}.tmp")
		val backup = File(parent, ".${destination.name}.${UUID.randomUUID()}.bak")
		var movedOld = false
		try {
			val input = contentResolver.openInputStream(uri)
				?: throw IllegalArgumentException("cannot open selected patch")
			var total = 0L
			input.use { source ->
				tmp.outputStream().use { output ->
					val buffer = ByteArray(64 * 1024)
					while (true) {
						val count = source.read(buffer)
						if (count < 0) break
						total += count
						if (total > maxBytes)
							throw IllegalArgumentException("selected patch is too large")
						output.write(buffer, 0, count)
					}
				}
			}
			if (total <= 0L)
				throw IllegalArgumentException("selected patch is empty")

			if (destination.exists()) {
				if (!destination.renameTo(backup))
					throw IllegalStateException("cannot protect existing local patch copy")
				movedOld = true
			}
			if (!tmp.renameTo(destination)) {
				if (movedOld)
					backup.renameTo(destination)
				throw IllegalStateException("cannot finalize imported patch")
			}
			if (movedOld)
				backup.delete()
		} finally {
			if (tmp.exists()) tmp.delete()
			if (backup.exists() && !destination.exists())
				backup.renameTo(destination)
			else if (backup.exists())
				backup.delete()
		}
	}

	private external fun nativeDocumentOpenResult(path: String?)

	companion object {
		init {
			System.loadLibrary("rack_engine")
		}
	}
}
