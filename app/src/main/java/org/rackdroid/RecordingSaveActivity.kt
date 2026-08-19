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

class RecordingSaveActivity : Activity() {

	private val requestCreateDocument = 1
	private lateinit var source: File

	override fun onCreate(savedInstanceState: Bundle?) {
		super.onCreate(savedInstanceState)

		val resolved = resolveSource()
		if (resolved == null) {
			Toast.makeText(this, R.string.toast_recording_save_failed, Toast.LENGTH_LONG).show()
			finish()
			return
		}
		source = resolved

		if (savedInstanceState != null)
			return

		val requested = intent?.getStringExtra(EXTRA_SAVE_FILENAME)
			?.trim().orEmpty().ifEmpty { source.name }
		val title = safeWavName(requested)

		val create = Intent(Intent.ACTION_CREATE_DOCUMENT).apply {
			addCategory(Intent.CATEGORY_OPENABLE)
			type = "audio/wav"
			putExtra(Intent.EXTRA_TITLE, title)
			addFlags(
				Intent.FLAG_GRANT_READ_URI_PERMISSION or
					Intent.FLAG_GRANT_WRITE_URI_PERMISSION or
					Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION
			)

			getSharedPreferences(PREFS, Context.MODE_PRIVATE)
				.getString(KEY_LAST_URI, null)
				?.let {
					runCatching {
						putExtra(DocumentsContract.EXTRA_INITIAL_URI, Uri.parse(it))
					}
				}
		}

		try {
			startActivityForResult(create, requestCreateDocument)
		} catch (_: Throwable) {
			saveFallback(R.string.toast_recording_save_picker_unavailable)
		}
	}

	@Deprecated("Deprecated in Java")
	override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
		super.onActivityResult(requestCode, resultCode, data)
		if (requestCode != requestCreateDocument)
			return

		val uri = if (resultCode == RESULT_OK) data?.data else null
		if (uri == null) {
			saveFallback(R.string.toast_recording_save_cancelled)
			return
		}

		val grantFlags = (data?.flags ?: 0) and
			(Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION)
		if (grantFlags != 0)
			runCatching { contentResolver.takePersistableUriPermission(uri, grantFlags) }

		getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
			.putString(KEY_LAST_URI, uri.toString())
			.apply()

		Thread {
			try {
				contentResolver.openOutputStream(uri, "w")?.use { out ->
					source.inputStream().use { input -> input.copyTo(out) }
				} ?: throw IllegalStateException("selected document output stream unavailable")

				val display = queryDisplayName(uri) ?: source.name
				runOnUiThread {
					Toast.makeText(
						this,
						getString(R.string.toast_recording_saved_as, display),
						Toast.LENGTH_LONG
					).show()
					finish()
				}
			} catch (_: Throwable) {
				runCatching { contentResolver.delete(uri, null, null) }
				saveFallbackBlocking(R.string.toast_recording_save_selected_failed)
			}
		}.start()
	}

	private fun resolveSource(): File? {
		val root = File(filesDir, "user/recordings").canonicalFile
		val raw = intent?.getStringExtra(EXTRA_SOURCE_PATH) ?: return null
		val candidate = runCatching { File(raw).canonicalFile }.getOrNull() ?: return null
		if (candidate.parentFile != root)
			return null
		if (!candidate.isFile || !candidate.name.endsWith(".wav", ignoreCase = true))
			return null
		return candidate
	}

	private fun queryDisplayName(uri: Uri): String? {
		contentResolver.query(
			uri,
			arrayOf(OpenableColumns.DISPLAY_NAME),
			null, null, null
		)?.use { cursor ->
			val index = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME)
			if (index >= 0 && cursor.moveToFirst())
				return cursor.getString(index)
		}
		return uri.lastPathSegment
	}

	private fun safeWavName(raw: String): String {
		var name = raw.replace('\\', '/').substringAfterLast('/')
			.filter { it.code >= 32 && it != '/' }
			.replace(Regex("[^A-Za-z0-9._ ()+\\-]"), "_")
			.trim()
			.take(160)
		if (name.isEmpty() || name == "." || name == "..")
			name = "rackdroid-recording.wav"
		if (!name.endsWith(".wav", ignoreCase = true))
			name += ".wav"
		return name
	}

	private fun saveFallback(messageRes: Int) {
		Thread { saveFallbackBlocking(messageRes) }.start()
	}

	private fun saveFallbackBlocking(messageRes: Int) {
		try {
			val coll = android.provider.MediaStore.Files.getContentUri("external")
			val values = android.content.ContentValues().apply {
				put(android.provider.MediaStore.MediaColumns.DISPLAY_NAME, source.name)
				put(android.provider.MediaStore.MediaColumns.MIME_TYPE, "audio/wav")
				put(android.provider.MediaStore.MediaColumns.RELATIVE_PATH, "Documents/RackDroid/")
			}
			val uri = contentResolver.insert(coll, values)
				?: throw IllegalStateException("MediaStore insert failed")
			contentResolver.openOutputStream(uri)?.use { out ->
				source.inputStream().use { input -> input.copyTo(out) }
			} ?: throw IllegalStateException("MediaStore output stream failed")

			runOnUiThread {
				Toast.makeText(
					this,
					getString(messageRes, source.name),
					Toast.LENGTH_LONG
				).show()
				finish()
			}
		} catch (_: Throwable) {
			runOnUiThread {
				Toast.makeText(
					this,
					R.string.toast_recording_save_failed,
					Toast.LENGTH_LONG
				).show()
				finish()
			}
		}
	}

	companion object {
		const val EXTRA_SOURCE_PATH = "org.rackdroid.extra.RECORDING_SOURCE_PATH"
		const val EXTRA_SAVE_FILENAME = "org.rackdroid.extra.RECORDING_SAVE_FILENAME"
		private const val PREFS = "recording_save"
		private const val KEY_LAST_URI = "last_uri"
	}
}
