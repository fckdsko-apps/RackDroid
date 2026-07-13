package org.rackdroid

import android.app.Activity
import android.app.Dialog
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.text.Editable
import android.text.TextWatcher
import android.util.LruCache
import android.view.Gravity
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import android.widget.EditText
import android.widget.FrameLayout
import android.widget.HorizontalScrollView
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.TextView
import androidx.recyclerview.widget.GridLayoutManager
import androidx.recyclerview.widget.RecyclerView
import java.io.File
import org.json.JSONArray

/** Geomini (OFL, assets/fonts) -- the app typeface, shared by every
 * Kotlin-side surface (toolbar, sheets, browser). Loaded once. */
object AppFont {
	private var tf: Typeface? = null
	fun get(ctx: android.content.Context): Typeface =
		tf ?: Typeface.createFromAsset(ctx.assets, "fonts/Geomini.ttf").also { tf = it }
}

/** Decoded module panel PNGs (native/host/main_ui_host.cpp's
 * --export-thumbnails, packaged as assets/thumbnails.zip and extracted to
 * filesDir/thumbnails/<key>.png at startup -- see asset_extract.cpp). Keyed
 * by model key, capped well under the default per-app heap so a full
 * ~160-module scroll can't OOM. Process-lifetime cache: models never change
 * at runtime, so nothing ever needs to be invalidated. */
private class SizedLruCache(maxBytes: Int) : LruCache<String, Bitmap>(maxBytes) {
	override fun sizeOf(key: String, value: Bitmap) = value.byteCount
}

internal object ThumbnailCache {
	private val cache = SizedLruCache(32 * 1024 * 1024) // 32MB of decoded pixels

	fun get(filesDir: File, key: String, targetWidthPx: Int): Bitmap? {
		cache.get(key)?.let { return it }
		val file = File(filesDir, "thumbnails/$key.webp")
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

private data class BrowserModel(
	val key: String,
	val name: String,
	val brand: String,
	val description: String,
	val plugin: String,
	val version: String,
	val license: String,
	var favorite: Boolean,
	val tags: List<String>,
)

/** Native replacement for Rack's canvas "Add module" browser: a real
 * search box with the system keyboard, brand/tag/favorite filter chips, and
 * a tap-to-place grid -- no more press-and-drag to add a module.
 *
 * Runs entirely against a JSON snapshot pulled once via [getModelsJson]
 * (native/port/browser_native.cpp builds it once and caches it; the plugin
 * list never changes after startup). Filtering/search happens client-side
 * against that snapshot -- no native round trip per keystroke.
 *
 * Must be a plain Dialog, not a DialogFragment: MainActivity is a
 * NativeActivity (no FragmentManager). Own window, like the menu sheet --
 * NativeActivity's main window never draws or receives touches for
 * anything added to it directly (see the invisible-overlay-button lesson
 * in MainActivity's toolbar comment). */
class ModuleBrowserSheet(
	private val activity: Activity,
	private val getModelsJson: () -> String,
	private val chooseModel: (String) -> Unit,
	private val setFavorite: (String, Boolean) -> Unit,
) {
	private fun dp(v: Int) = (v * activity.resources.displayMetrics.density).toInt()

	private var allModels: List<BrowserModel> = emptyList()
	private var brands: List<String> = emptyList()
	private var tags: List<String> = emptyList()

	private var query = ""
	private var brandFilter: String? = null
	private var tagFilter: String? = null
	private var favoritesOnly = false

	private lateinit var adapter: ModuleAdapter
	private lateinit var countLabel: TextView
	private lateinit var brandRow: LinearLayout
	private lateinit var tagRow: LinearLayout

	fun show() {
		parseModels()

		val glass = activity as? MainActivity
		val blurOn = activity.windowManager.isCrossWindowBlurEnabled
		val root = LinearLayout(activity).apply {
			orientation = LinearLayout.VERTICAL
			// Fullscreen glass: the rack shows through, blurred, behind the
			// whole browser (solid warm dark when blur is unavailable).
			setBackgroundColor(Color.parseColor(if (blurOn) "#D9221F1A" else "#221F1A"))
		}

		val dialog = Dialog(activity, android.R.style.Theme_Black_NoTitleBar_Fullscreen)
		dialog.setContentView(root)
		dialog.window?.setBackgroundDrawable(
			android.graphics.drawable.ColorDrawable(Color.TRANSPARENT))
		glass?.glassify(dialog.window, 56)
		glass?.trackTopWindow(dialog)

		// -- search bar --
		val searchRow = LinearLayout(activity).apply {
			orientation = LinearLayout.HORIZONTAL
			gravity = Gravity.CENTER_VERTICAL
			setPadding(dp(12), dp(12), dp(12), dp(6))
		}
		val search = EditText(activity).apply {
			hint = activity.getString(R.string.browser_search_hint)
			typeface = AppFont.get(activity)
			setHintTextColor(Color.parseColor("#9A9284"))
			setTextColor(Color.parseColor("#EDE6D8"))
			setSingleLine()
			layoutParams = LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f)
			addTextChangedListener(object : TextWatcher {
				override fun afterTextChanged(s: Editable?) {
					query = s?.toString().orEmpty()
					refresh()
				}
				override fun beforeTextChanged(s: CharSequence?, a: Int, b: Int, c: Int) {}
				override fun onTextChanged(s: CharSequence?, a: Int, b: Int, c: Int) {}
			})
		}
		searchRow.addView(search)
		searchRow.addView(TextView(activity).apply {
			text = "✕"
			setTextColor(Color.parseColor("#9A9284"))
			setPadding(dp(16), 0, dp(4), 0)
			setOnClickListener { dialog.dismiss() }
		})
		root.addView(searchRow)

		// -- favorites + brand chips --
		brandRow = LinearLayout(activity).apply { orientation = LinearLayout.HORIZONTAL }
		val brandScroll = HorizontalScrollView(activity).apply {
			isHorizontalScrollBarEnabled = false
			addView(brandRow)
		}
		root.addView(brandScroll)

		// -- tag chips --
		tagRow = LinearLayout(activity).apply { orientation = LinearLayout.HORIZONTAL }
		val tagScroll = HorizontalScrollView(activity).apply {
			isHorizontalScrollBarEnabled = false
			addView(tagRow)
		}
		root.addView(tagScroll)

		countLabel = TextView(activity).apply {
			typeface = AppFont.get(activity)
			setTextColor(Color.parseColor("#9A9284"))
			textSize = 12f
			setPadding(dp(12), dp(6), dp(12), dp(6))
		}
		root.addView(countLabel)

		// -- results grid --
		val recycler = RecyclerView(activity).apply {
			layoutParams = LinearLayout.LayoutParams(
				ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f)
			layoutManager = GridLayoutManager(activity, 3)
			setPadding(dp(6), 0, dp(6), dp(6))
			clipToPadding = false
			// Tiles drift up + fade in as a wave whenever results change.
			layoutAnimation = android.view.animation.LayoutAnimationController(
				android.view.animation.AnimationSet(true).apply {
					addAnimation(android.view.animation.AlphaAnimation(0f, 1f))
					addAnimation(android.view.animation.TranslateAnimation(
						0f, 0f, dp(28).toFloat(), 0f))
					duration = 220
					interpolator = android.view.animation.DecelerateInterpolator()
				}, 0.05f)
		}
		adapter = ModuleAdapter(
			filesDir = activity.filesDir,
			targetWidthPx = activity.resources.displayMetrics.widthPixels / 3,
			onTap = { model -> chooseModel(model.key); dialog.dismiss() },
			onToggleFavorite = { model ->
				model.favorite = !model.favorite
				setFavorite(model.key, model.favorite)
				adapter.notifyDataSetChanged()
			},
			onInfo = { model -> showInfoDialog(model) },
		)
		recycler.adapter = adapter
		recyclerRef = recycler
		root.addView(recycler)

		buildFilterChips()
		refresh()
		dialog.show()
	}

	private fun parseModels() {
		val json = getModelsJson()
		val arr = try { JSONArray(json) } catch (_: Exception) { JSONArray() }
		val list = ArrayList<BrowserModel>(arr.length())
		for (i in 0 until arr.length()) {
			val o = arr.getJSONObject(i)
			val tagsArr = o.getJSONArray("tags")
			val tagList = ArrayList<String>(tagsArr.length())
			for (j in 0 until tagsArr.length()) tagList.add(tagsArr.getString(j))
			list.add(BrowserModel(
				key = o.getString("key"),
				name = o.getString("name"),
				brand = o.getString("brand"),
				description = o.optString("description"),
				plugin = o.optString("plugin"),
				version = o.optString("version"),
				license = o.optString("license"),
				favorite = o.getBoolean("favorite"),
				tags = tagList,
			))
		}
		allModels = list
		brands = list.map { it.brand }.distinct().sorted()
		tags = list.flatMap { it.tags }.distinct().sorted()
	}

	private fun chip(label: String, initiallyActive: Boolean, onToggle: (Boolean) -> Unit): TextView {
		return TextView(activity).apply {
			text = label
			typeface = AppFont.get(activity)
			textSize = 12f
			setPadding(dp(12), dp(6), dp(12), dp(6))
			val margin = dp(4)
			layoutParams = LinearLayout.LayoutParams(
				ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT
			).apply { setMargins(margin, margin, margin, margin) }
			fun paint(active: Boolean) {
				background = GradientDrawable().apply {
					cornerRadius = dp(18).toFloat()
					setColor(Color.parseColor(if (active) "#FFFFFF" else "#3A352D"))
					if (!active) setStroke(dp(1), Color.parseColor("#1FFFFFFF"))
				}
				setTextColor(Color.parseColor(if (active) "#17140F" else "#EDE6D8"))
			}
			var active = initiallyActive
			paint(active)
			setOnClickListener {
				active = !active
				paint(active)
				onToggle(active)
			}
		}
	}

	private fun buildFilterChips() {
		brandRow.removeAllViews()
		val favoritesLabel = activity.getString(R.string.browser_favorites)
		brandRow.addView(chip(favoritesLabel, favoritesOnly) { active ->
			favoritesOnly = active
			refresh()
		})
		for (brand in brands) {
			brandRow.addView(chip(brand, brandFilter == brand) { active ->
				brandFilter = if (active) brand else null
				// Only one brand active at a time: repaint siblings.
				for (i in 0 until brandRow.childCount) {
					val v = brandRow.getChildAt(i) as? TextView ?: continue
					if (v.text != brand && v.text != favoritesLabel) {
						v.background = GradientDrawable().apply {
							cornerRadius = dp(18).toFloat()
							setColor(Color.parseColor("#3A352D"))
							setStroke(dp(1), Color.parseColor("#1FFFFFFF"))
						}
						v.setTextColor(Color.parseColor("#EDE6D8"))
					}
				}
				refresh()
			})
		}
		tagRow.removeAllViews()
		for (tag in tags) {
			tagRow.addView(chip(tag, tagFilter == tag) { active ->
				tagFilter = if (active) tag else null
				for (i in 0 until tagRow.childCount) {
					val v = tagRow.getChildAt(i) as? TextView ?: continue
					if (v.text != tag) {
						v.background = GradientDrawable().apply {
							cornerRadius = dp(18).toFloat()
							setColor(Color.parseColor("#3A352D"))
							setStroke(dp(1), Color.parseColor("#1FFFFFFF"))
						}
						v.setTextColor(Color.parseColor("#EDE6D8"))
					}
				}
				refresh()
			})
		}
	}

	private var recyclerRef: RecyclerView? = null

	private fun refresh() {
		val q = query.trim().lowercase()
		val filtered = allModels.filter { m ->
			(!favoritesOnly || m.favorite) &&
				(brandFilter == null || m.brand == brandFilter) &&
				(tagFilter == null || m.tags.contains(tagFilter)) &&
				(q.isEmpty() || m.name.lowercase().contains(q) ||
					m.brand.lowercase().contains(q) ||
					m.tags.any { it.lowercase().contains(q) })
		}
		countLabel.text = activity.getString(R.string.browser_module_count, filtered.size)
		adapter.submit(filtered)
		recyclerRef?.scheduleLayoutAnimation()
	}

	/** Module info card, opened by holding a grid tile (see LONG_PRESS_MS). */
	private fun showInfoDialog(m: BrowserModel) {
		val content = LinearLayout(activity).apply {
			orientation = LinearLayout.VERTICAL
			setPadding(dp(20), dp(16), dp(20), dp(16))
		}
		fun line(text: String, color: String, size: Float, bold: Boolean = false, padTop: Int = 0) {
			if (text.isEmpty()) return
			content.addView(TextView(activity).apply {
				this.text = text
				setTextColor(Color.parseColor(color))
				textSize = size
				setTypeface(AppFont.get(activity), if (bold) Typeface.BOLD else Typeface.NORMAL)
				setPadding(0, dp(padTop), 0, 0)
			})
		}
		line(m.name, "#EDE6D8", 18f, bold = true)
		line(m.brand, "#FFDA9F", 13f)
		line(m.description, "#D8D0C2", 14f, padTop = 10)
		if (m.tags.isNotEmpty())
			line(m.tags.joinToString(" · "), "#C0A377", 12f, padTop = 10)
		val meta = listOf(m.plugin, m.version, m.license).filter { it.isNotEmpty() }
		line(meta.joinToString(" — "), "#9A9284", 11f, padTop = 10)

		val blurOn = activity.windowManager.isCrossWindowBlurEnabled
		Dialog(activity).apply {
			setContentView(android.widget.ScrollView(activity).apply { addView(content) })
			// The default dialog window background is light and pokes out
			// above the content -- replace it with the glass card itself.
			window?.setBackgroundDrawable(GradientDrawable().apply {
				cornerRadius = dp(20).toFloat()
				setColor(Color.parseColor(if (blurOn) "#CC221F1A" else "#F5221F1A"))
				setStroke(dp(1), Color.parseColor("#2EFFFFFF"))
			})
			window?.setLayout(
				(activity.resources.displayMetrics.widthPixels * 0.85).toInt(),
				ViewGroup.LayoutParams.WRAP_CONTENT)
			(activity as? MainActivity)?.glassify(window, 48)
			(activity as? MainActivity)?.trackTopWindow(this)
			show()
		}
	}

	private class ModuleAdapter(
		val filesDir: File,
		val targetWidthPx: Int,
		val onTap: (BrowserModel) -> Unit,
		val onToggleFavorite: (BrowserModel) -> Unit,
		val onInfo: (BrowserModel) -> Unit,
	) : RecyclerView.Adapter<ModuleAdapter.Holder>() {
		companion object {
			/** Hold a tile this long for the info card (per user request;
			 * deliberately longer than the ~500ms system long-press). */
			const val LONG_PRESS_MS = 3000L
		}
		private var items: List<BrowserModel> = emptyList()

		fun submit(newItems: List<BrowserModel>) {
			items = newItems
			notifyDataSetChanged()
		}

		class Holder(val root: FrameLayout, val image: ImageView, val caption: LinearLayout,
				val name: TextView, val brand: TextView, val tags: TextView, val star: TextView)
			: RecyclerView.ViewHolder(root)

		override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): Holder {
			val ctx = parent.context
			val density = ctx.resources.displayMetrics.density
			fun dp(v: Int) = (v * density).toInt()

			val card = LinearLayout(ctx).apply {
				orientation = LinearLayout.VERTICAL
				background = GradientDrawable().apply {
					cornerRadius = dp(14).toFloat()
					setColor(Color.parseColor("#2B2721"))
					setStroke(dp(1), Color.parseColor("#17FFFFFF"))
				}
				clipToOutline = true
			}
			// Real panel thumbnail (native/host/main_ui_host.cpp
			// --export-thumbnails), shown when present; falls back to a
			// brand/name/tag text block otherwise (missing/failed export).
			val image = ImageView(ctx).apply {
				scaleType = ImageView.ScaleType.FIT_CENTER
				adjustViewBounds = true
				visibility = View.GONE
			}
			card.addView(image, LinearLayout.LayoutParams(
				ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT))

			val name = TextView(ctx).apply {
				typeface = AppFont.get(ctx)
				setTextColor(Color.parseColor("#EDE6D8"))
				setTypeface(typeface, Typeface.BOLD)
				textSize = 13f
				maxLines = 2
			}
			val brand = TextView(ctx).apply {
				typeface = AppFont.get(ctx)
				setTextColor(Color.parseColor("#9A9284"))
				textSize = 10f
			}
			val tags = TextView(ctx).apply {
				typeface = AppFont.get(ctx)
				setTextColor(Color.parseColor("#C0A377"))
				textSize = 9f
				maxLines = 1
			}
			val caption = LinearLayout(ctx).apply {
				orientation = LinearLayout.VERTICAL
				setPadding(dp(8), dp(6), dp(8), dp(8))
				addView(name)
				addView(brand)
				addView(tags)
			}
			card.addView(caption)

			val star = TextView(ctx).apply {
				text = "☆"
				textSize = 16f
				setPadding(dp(6), dp(4), dp(6), dp(4))
			}
			val root = FrameLayout(ctx).apply {
				val margin = dp(4)
				layoutParams = ViewGroup.MarginLayoutParams(
					ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT
				).apply { setMargins(margin, margin, margin, margin) }
				addView(card, FrameLayout.LayoutParams(
					ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT))
				addView(star, FrameLayout.LayoutParams(
					ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT,
					Gravity.TOP or Gravity.END))
			}
			return Holder(root, image, caption, name, brand, tags, star)
		}

		override fun onBindViewHolder(holder: Holder, position: Int) {
			val m = items[position]
			val thumb = ThumbnailCache.get(filesDir, m.key, targetWidthPx)
			// The caption (title) always shows, so every tile is identifiable
			// by name; the thumbnail sits above it, tags only in the
			// no-thumbnail fallback where the extra text carries the tile.
			holder.caption.visibility = View.VISIBLE
			if (thumb != null) {
				holder.image.setImageBitmap(thumb)
				holder.image.visibility = View.VISIBLE
				holder.tags.visibility = View.GONE
			}
			else {
				holder.image.visibility = View.GONE
				holder.tags.visibility = View.VISIBLE
			}
			holder.name.text = m.name
			holder.brand.text = m.brand
			holder.tags.text = m.tags.joinToString(" · ")
			holder.star.text = if (m.favorite) "★" else "☆"
			holder.star.setTextColor(Color.parseColor(if (m.favorite) "#FFDA9F" else "#9A9284"))
			holder.star.setOnClickListener { onToggleFavorite(m) }
			// Tap places the module; holding LONG_PRESS_MS opens the info
			// card instead (and the flag suppresses the click that Android
			// still delivers on finger-up).
			var infoFired = false
			val showInfo = Runnable { infoFired = true; onInfo(m) }
			holder.root.setOnTouchListener { v, ev ->
				when (ev.actionMasked) {
					MotionEvent.ACTION_DOWN -> { infoFired = false; v.postDelayed(showInfo, LONG_PRESS_MS) }
					MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> v.removeCallbacks(showInfo)
				}
				false
			}
			holder.root.setOnClickListener { if (!infoFired) onTap(m) }
		}

		override fun getItemCount() = items.size
	}
}
