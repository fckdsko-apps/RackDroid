package org.rackdroid

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Typeface
import android.util.LruCache
import java.io.File

/** Geomini (OFL, assets/fonts) -- the app typeface, shared by every
 * Kotlin-side surface (toolbar, sheets, palette). Loaded once. */
object AppFont {
	private var tf: Typeface? = null
	fun get(ctx: android.content.Context): Typeface =
		tf ?: Typeface.createFromAsset(ctx.assets, "fonts/Geomini.ttf").also { tf = it }
}

/** Decoded module panel art (native/host/main_ui_host.cpp's
 * --export-thumbnails), keyed by model key, capped well under the default
 * per-app heap so a full scroll of the whole catalogue can't OOM.
 * Process-lifetime cache: models never change at runtime, so nothing ever
 * needs to be invalidated. */
private class SizedLruCache(maxBytes: Int) : LruCache<String, Bitmap>(maxBytes) {
	override fun sizeOf(key: String, value: Bitmap) = value.byteCount
}

internal object ThumbnailCache {
	private val cache = SizedLruCache(32 * 1024 * 1024) // 32MB of decoded pixels

	fun get(filesDir: File, key: String, targetWidthPx: Int): Bitmap? {
		cache.get(key)?.let { return it }
		// Bundled plugins (Core/Fundamental/RackDroidDrums) have their tile
		// art under the shared thumbnails/ tree extracted from thumbnails.zip.
		// Every other plugin's art ships inside its own .rdmod instead
		// (scripts/make_rdmods.sh packs it as thumbs/<modelSlug>.webp) and
		// lands under its private install dir, filesDir/user/plugins/<slug>/ --
		// key is always "pluginSlug/modelSlug" (see browser_native.cpp), so
		// both lookups reuse it as-is.
		var file = File(filesDir, "thumbnails/$key.webp")
		if (!file.exists()) {
			val pluginSlug = key.substringBefore('/')
			val modelSlug = key.substringAfter('/')
			file = File(filesDir, "user/plugins/$pluginSlug/thumbs/$modelSlug.webp")
		}
		if (!file.exists())
			return null
		val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
		BitmapFactory.decodeFile(file.path, bounds)
		if (bounds.outWidth <= 0)
			return null
		var sample = 1
		while (bounds.outWidth / (sample * 2) >= targetWidthPx)
			sample *= 2
		val bmp = BitmapFactory.decodeFile(file.path, BitmapFactory.Options().apply { inSampleSize = sample })
			?: return null
		cache.put(key, bmp)
		return bmp
	}
}
