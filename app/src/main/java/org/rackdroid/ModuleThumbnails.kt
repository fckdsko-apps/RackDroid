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
	private const val MIB = 1024 * 1024
	private const val MIN_CACHE_BYTES = 8 * MIB
	private const val MAX_CACHE_BYTES = 32 * MIB
	// ComponentCallbacks2's named constants are deprecated at API 35, but the
	// callback still delivers the documented level values on minSdk 29 devices.
	private const val TRIM_RUNNING_LOW = 10
	private const val TRIM_MODERATE = 60
	private val cache = SizedLruCache(MAX_CACHE_BYTES)
	private var budgetBytes = MAX_CACHE_BYTES

	/** Size decoded art to the device instead of reserving the same 32MB on a
	 * low-RAM phone and a flagship. One twelfth of the Java heap leaves ample
	 * room for the palette/views while the 8–32MB clamp keeps scrolling useful. */
	fun configure(context: android.content.Context) {
		val am = context.getSystemService(android.content.Context.ACTIVITY_SERVICE)
			as android.app.ActivityManager
		val heapBudget = (am.memoryClass.toLong() * MIB / 12L)
			.coerceIn(MIN_CACHE_BYTES.toLong(), MAX_CACHE_BYTES.toLong()).toInt()
		budgetBytes = if (am.isLowRamDevice) minOf(heapBudget, 12 * MIB) else heapBudget
		cache.resize(budgetBytes)
	}

	/** Release artwork promptly when Android asks. The files remain on disk and
	 * are decoded lazily again if the user returns to the palette. */
	fun trim(level: Int) {
		when {
			level >= TRIM_MODERATE -> cache.evictAll()
			level >= TRIM_RUNNING_LOW -> cache.trimToSize(budgetBytes / 2)
		}
	}

	fun clear() = cache.evictAll()

	fun removePlugin(slug: String) {
		val prefix = "$slug/"
		cache.snapshot().keys.filter { it.startsWith(prefix) }.forEach(cache::remove)
	}

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
