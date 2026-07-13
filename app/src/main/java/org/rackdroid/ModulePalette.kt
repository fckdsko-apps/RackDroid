package org.rackdroid

import android.app.Activity
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.view.Gravity
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import android.widget.FrameLayout
import android.widget.HorizontalScrollView
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.PopupWindow
import android.widget.ScrollView
import android.widget.TextView
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import org.json.JSONArray

/** Quick module palette: a bottom bar of type chips (VCO, VCF, VCA, …) that
 * opens a horizontal, scrollable strip of module thumbnails. Tap a tile to
 * place the module at the center of the screen; LONG-PRESS and DRAG it up
 * onto the rack to drop it exactly where the finger lands (the native side
 * moves the virtual cursor to the drop point before placing).
 *
 * Toggled by the 🧩 button in the bottom tools bar; sits just above it. */
class ModulePalette(
	private val activity: Activity,
	private val getModelsJson: () -> String,
	private val requestBuild: () -> Unit,
	private val chooseAt: (String, Float, Float) -> Unit,
) {
	private fun dp(v: Int) = (v * activity.resources.displayMetrics.density).toInt()

	private data class Entry(val key: String, val name: String, val brand: String,
		val tags: List<String>, val description: String, val plugin: String)

	/** Chip label -> tag predicate. Labels are the universal modular
	 * shorthand, deliberately not localized. */
	private val categories: List<Pair<String, (Entry) -> Boolean>> = listOf(
		"VCO" to { e -> "Oscillator" in e.tags && "Low-frequency oscillator" !in e.tags },
		"LFO" to { e -> "Low-frequency oscillator" in e.tags },
		"VCF" to { e -> "Filter" in e.tags },
		"VCA" to { e -> "Voltage-controlled amplifier" in e.tags },
		"ENV" to { e -> "Envelope generator" in e.tags },
		"SEQ" to { e -> "Sequencer" in e.tags },
		"DRUM" to { e -> "Drum" in e.tags },
		"MIX" to { e -> "Mixer" in e.tags },
		"FX" to { e -> e.tags.any { it in setOf("Effect", "Delay", "Reverb", "Chorus", "Distortion", "Flanger", "Phaser") } },
		"NOISE" to { e -> "Noise" in e.tags },
		"QNT" to { e -> "Quantizer" in e.tags },
		"MIDI" to { e -> "MIDI" in e.tags },
		"UTIL" to { e -> "Utility" in e.tags },
	)

	private var popup: PopupWindow? = null
	private var ghost: PopupWindow? = null
	private var ghostW = 0
	private var ghostH = 0
	private var entries: List<Entry> = emptyList()
	private lateinit var strip: RecyclerView
	private lateinit var chipRow: LinearLayout
	private var activeChip: TextView? = null
	private val adapter = TileAdapter()

	fun toggle() {
		if (popup != null) hide() else show()
	}

	/** Re-read the model list after new packs are installed at runtime. Forces
	 * a native rebuild, then repopulates a short delay later (the build hops to
	 * the render thread). The next chip tap shows the freshly added modules. */
	fun reload() {
		entries = emptyList()
		requestBuild()
		android.os.Handler(activity.mainLooper).postDelayed({
			runCatching { loadEntries() }
			activeChip?.let { chip ->
				// A category is open: re-filter it so new modules appear now.
				chip.performClick(); chip.performClick()
			}
		}, 450)
	}

	fun hide() {
		popup?.dismiss()
		popup = null
	}

	/** Collapse only the open tile strip (the "modules menu"), keeping the chip
	 * bar itself visible -- same as tapping the active chip again. */
	private fun collapseStrip() {
		val chip = activeChip ?: return
		chip.background = chipBg(false)
		chip.setTextColor(Color.parseColor("#EDE6D8"))
		activeChip = null
		if (::strip.isInitialized) strip.visibility = View.GONE
	}

	/** Palette container: a downward swipe closes the OPEN tile strip (not the
	 * whole bar). Engages only when a category is open and only for a clearly
	 * DOWNWARD drag past the touch slop, so tapping/scrolling chips and
	 * long-press-drag of a tile UP onto the rack all keep working. */
	private inner class SwipeDownPalette(ctx: android.content.Context) : android.widget.FrameLayout(ctx) {
		private var startRawY = 0f
		private var startRawX = 0f
		private var dragging = false
		private val slop = android.view.ViewConfiguration.get(ctx).scaledTouchSlop

		override fun onInterceptTouchEvent(ev: MotionEvent): Boolean {
			when (ev.actionMasked) {
				MotionEvent.ACTION_DOWN -> { startRawY = ev.rawY; startRawX = ev.rawX; dragging = false }
				MotionEvent.ACTION_MOVE -> {
					if (activeChip == null) return false // nothing open to close
					val dy = ev.rawY - startRawY
					if (dy > slop * 1.5f && dy > kotlin.math.abs(ev.rawX - startRawX)) {
						dragging = true; return true
					}
				}
			}
			return false
		}

		override fun onTouchEvent(ev: MotionEvent): Boolean {
			when (ev.actionMasked) {
				MotionEvent.ACTION_MOVE -> if (dragging) {
					translationY = (ev.rawY - startRawY).coerceAtLeast(0f); return true
				}
				MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> if (dragging) {
					dragging = false
					if (translationY > dp(36)) collapseStrip()
					animate().translationY(0f).setDuration(150L)
						.setInterpolator(android.view.animation.DecelerateInterpolator()).start()
					return true
				}
			}
			return false
		}
	}

	/** Show the persistent palette bar (idempotent). */
	fun show() {
		if (popup != null)
			return
		if (entries.isEmpty())
			loadEntries()

		val font = AppFont.get(activity)
		strip = RecyclerView(activity).apply {
			layoutManager = LinearLayoutManager(activity, LinearLayoutManager.HORIZONTAL, false)
			this.adapter = this@ModulePalette.adapter
			setBackgroundColor(Color.TRANSPARENT)
			setPadding(dp(6), dp(8), dp(6), dp(4))
			clipToPadding = false
			visibility = View.GONE
			layoutAnimation = android.view.animation.LayoutAnimationController(
				android.view.animation.AnimationSet(true).apply {
					addAnimation(android.view.animation.AlphaAnimation(0f, 1f))
					addAnimation(android.view.animation.TranslateAnimation(
						dp(24).toFloat(), 0f, 0f, 0f))
					duration = 180
					interpolator = android.view.animation.DecelerateInterpolator()
				}, 0.04f)
		}
		chipRow = LinearLayout(activity).apply { orientation = LinearLayout.HORIZONTAL }
		for ((label, pred) in categories) {
			chipRow.addView(TextView(activity).apply {
				text = label
				textSize = 12f
				setTypeface(font, Typeface.BOLD)
				setTextColor(Color.parseColor("#EDE6D8"))
				background = chipBg(false)
				setPadding(dp(14), dp(7), dp(14), dp(7))
				layoutParams = LinearLayout.LayoutParams(
					ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT
				).apply { setMargins(dp(4), dp(6), dp(4), dp(8)) }
				setOnClickListener { selectChip(this, label, pred) }
			})
		}
		val chipScroll = HorizontalScrollView(activity).apply {
			isHorizontalScrollBarEnabled = false
			addView(chipRow)
		}
		val card = LinearLayout(activity).apply {
			orientation = LinearLayout.VERTICAL
			background = GradientDrawable().apply {
				cornerRadius = dp(20).toFloat()
				setColor(Color.parseColor("#D9221F1A"))
				setStroke(dp(1), Color.parseColor("#26FFFFFF"))
			}
			clipToOutline = true
			addView(strip, LinearLayout.LayoutParams(
				ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT))
			addView(chipScroll)
		}
		val content = SwipeDownPalette(activity).apply {
			setPadding(dp(8), 0, dp(8), 0)
			addView(card)
		}
		val p = PopupWindow(content,
			ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT)
		p.isFocusable = false
		popup = p
		val decor = activity.window.decorView
		decor.post {
			runCatching {
				val bottom = decor.rootWindowInsets?.getInsets(
					android.view.WindowInsets.Type.systemBars())?.bottom ?: 0
				p.showAtLocation(decor, Gravity.BOTTOM or Gravity.START, 0, bottom + dp(6))
				card.alpha = 0f
				card.translationY = dp(32).toFloat()
				card.animate().alpha(1f).translationY(0f).setDuration(260L)
					.setInterpolator(android.view.animation.DecelerateInterpolator()).start()
			}
		}
	}

	private fun chipBg(active: Boolean) = GradientDrawable().apply {
		cornerRadius = dp(16).toFloat()
		setColor(Color.parseColor(if (active) "#FFFFFF" else "#3A352D"))
		if (!active) setStroke(dp(1), Color.parseColor("#1FFFFFFF"))
	}

	private fun selectChip(chip: TextView, label: String, pred: (Entry) -> Boolean) {
		if (activeChip === chip) {
			// Tap the active chip again: collapse the strip.
			chip.background = chipBg(false)
			chip.setTextColor(Color.parseColor("#EDE6D8"))
			activeChip = null
			strip.visibility = View.GONE
			return
		}
		activeChip?.background = chipBg(false)
		activeChip?.setTextColor(Color.parseColor("#EDE6D8"))
		activeChip = chip
		chip.background = chipBg(true)
		chip.setTextColor(Color.parseColor("#17140F"))

		if (entries.isEmpty())
			loadEntries()
		// Beginner-friendly ordering: the stock VCV modules first, then A-Z.
		val items = entries.filter(pred).sortedWith(
			compareBy({ it.brand != "VCV" }, { it.name }))
		adapter.submit(items)
		strip.visibility = View.VISIBLE
		strip.scheduleLayoutAnimation()
	}

	private fun loadEntries() {
		var json = getModelsJson()
		if (json.length < 4) {
			// Not built yet: ask the render thread and retry shortly.
			requestBuild()
			android.os.Handler(activity.mainLooper).postDelayed({ loadEntries() }, 500)
			return
		}
		val arr = try { JSONArray(json) } catch (_: Exception) { JSONArray() }
		val list = ArrayList<Entry>(arr.length())
		for (i in 0 until arr.length()) {
			val o = arr.getJSONObject(i)
			val tagsArr = o.getJSONArray("tags")
			val tags = ArrayList<String>(tagsArr.length())
			for (j in 0 until tagsArr.length()) tags.add(tagsArr.getString(j))
			list.add(Entry(o.getString("key"), o.getString("name"), o.optString("brand"),
				tags, o.optString("description"), o.optString("plugin")))
		}
		entries = list
	}

	/** Info popup for a module (the ⓘ badge on each tile): what it is and what
	 * it does, from the model's own metadata (name, brand, tags, description). */
	private fun showInfo(e: Entry) {
		val ctx = activity
		val font = AppFont.get(ctx)
		val col = LinearLayout(ctx).apply {
			orientation = LinearLayout.VERTICAL
			setPadding(dp(22), dp(20), dp(22), dp(20))
		}
		col.addView(TextView(ctx).apply {
			text = e.name
			setTextColor(Color.parseColor("#FFDA9F"))
			textSize = 19f
			setTypeface(font, Typeface.BOLD)
		})
		val sub = listOf(e.brand, e.plugin).filter { it.isNotBlank() }.distinct().joinToString(" • ")
		if (sub.isNotEmpty()) col.addView(TextView(ctx).apply {
			text = sub
			setTextColor(Color.parseColor("#9C9486"))
			textSize = 13f
			setPadding(0, dp(3), 0, 0)
		})
		if (e.tags.isNotEmpty()) col.addView(TextView(ctx).apply {
			text = e.tags.joinToString("   ") { "#$it" }
			setTextColor(Color.parseColor("#8FB98F"))
			textSize = 12f
			setPadding(0, dp(12), 0, 0)
		})
		col.addView(TextView(ctx).apply {
			text = e.description.ifBlank { ctx.getString(R.string.module_no_description) }
			setTextColor(Color.parseColor("#E4DCCB"))
			textSize = 15f
			setPadding(0, dp(14), 0, 0)
			setLineSpacing(dp(3).toFloat(), 1f)
		})
		val dlg = android.app.AlertDialog.Builder(ctx).create()
		dlg.setView(ScrollView(ctx).apply { addView(col); isVerticalScrollBarEnabled = false })
		dlg.setButton(android.content.DialogInterface.BUTTON_POSITIVE,
			ctx.getString(android.R.string.ok)) { d, _ -> d.dismiss() }
		dlg.window?.setBackgroundDrawable(GradientDrawable().apply {
			cornerRadius = dp(22).toFloat()
			setColor(Color.parseColor("#F2221F1A"))
			setStroke(dp(1), Color.parseColor("#2EFFFFFF"))
		})
		(ctx as? MainActivity)?.let { runCatching { it.trackTopWindow(dlg); it.glassify(dlg.window) } }
		dlg.show()
	}

	// ---- drag & drop ----

	private fun showGhost(key: String, atX: Float, atY: Float) {
		val bmp = ThumbnailCache.get(activity.filesDir, key, dp(120)) ?: return
		ghostH = dp(120)
		ghostW = if (bmp.height > 0) ghostH * bmp.width / bmp.height else dp(40)
		val iv = ImageView(activity).apply {
			setImageBitmap(bmp)
			scaleType = ImageView.ScaleType.FIT_CENTER
			alpha = 0.9f
			background = GradientDrawable().apply {
				cornerRadius = dp(6).toFloat()
				setStroke(dp(2), Color.parseColor("#66FFDA9F"))
			}
		}
		val g = PopupWindow(iv, ghostW, ghostH)
		g.isTouchable = false
		ghost = g
		runCatching {
			// Ghost CENTERED on the finger, so what you see is where it lands.
			g.showAtLocation(activity.window.decorView, Gravity.TOP or Gravity.START,
				atX.toInt() - ghostW / 2, atY.toInt() - ghostH / 2)
			iv.scaleX = 0.6f; iv.scaleY = 0.6f
			iv.animate().scaleX(1f).scaleY(1f).setDuration(140L)
				.setInterpolator(android.view.animation.OvershootInterpolator()).start()
		}
	}

	private fun moveGhost(x: Float, y: Float) {
		ghost?.update(x.toInt() - ghostW / 2, y.toInt() - ghostH / 2, -1, -1)
	}

	private fun hideGhost() {
		ghost?.dismiss()
		ghost = null
	}

	private inner class TileAdapter : RecyclerView.Adapter<TileAdapter.Holder>() {
		private var items: List<Entry> = emptyList()

		fun submit(list: List<Entry>) {
			items = list
			notifyDataSetChanged()
		}

		inner class Holder(val root: LinearLayout, val image: ImageView,
			val name: TextView, val info: TextView)
			: RecyclerView.ViewHolder(root)

		override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): Holder {
			// The thumbnail fills a FIXED-height box (MATCH_PARENT into a
			// dp(110)-tall frame) so every tile is exactly the same height and
			// the row stays perfectly aligned -- the badge, being an overlay,
			// can never push the box taller (that skewed the strip before).
			val image = ImageView(activity).apply {
				adjustViewBounds = true
				scaleType = ImageView.ScaleType.FIT_CENTER
				layoutParams = FrameLayout.LayoutParams(
					ViewGroup.LayoutParams.WRAP_CONTENT,
					ViewGroup.LayoutParams.MATCH_PARENT)
			}
			// ⓘ info badge, top-right corner of the thumbnail.
			val info = TextView(activity).apply {
				text = "ⓘ"
				textSize = 12f
				gravity = Gravity.CENTER
				setTextColor(Color.parseColor("#17140F"))
				background = GradientDrawable().apply {
					shape = GradientDrawable.OVAL
					setColor(Color.parseColor("#E6FFDA9F"))
					setStroke(dp(1), Color.parseColor("#66000000"))
				}
				layoutParams = FrameLayout.LayoutParams(dp(22), dp(22)).apply {
					gravity = Gravity.TOP or Gravity.END
					topMargin = dp(3); rightMargin = dp(3)
				}
			}
			val imageWrap = FrameLayout(activity).apply {
				// Fixed height + a minimum width keep tiles uniform even when a
				// module has no thumbnail (its image collapses to zero width).
				minimumWidth = dp(56)
				layoutParams = LinearLayout.LayoutParams(
					ViewGroup.LayoutParams.WRAP_CONTENT, dp(110))
				addView(image)
				addView(info)
			}
			// The label must NOT widen the tile: a long name (kept at up to
			// dp(96) before) made the tile wider than its thumbnail, and the
			// centered thumbnail + corner badge then looked off-centre. Fill
			// the thumbnail's width instead and ellipsize anything longer.
			val name = TextView(activity).apply {
				textSize = 10f
				typeface = AppFont.get(activity)
				setTextColor(Color.parseColor("#C9C1B2"))
				gravity = Gravity.CENTER_HORIZONTAL
				// Wrap onto a second line rather than truncating; still bound to
				// the thumbnail's width so it can't widen (and off-centre) the
				// tile. Only a name longer than two lines gets an ellipsis.
				maxLines = 2
				ellipsize = android.text.TextUtils.TruncateAt.END
				setLineSpacing(0f, 0.95f)
				layoutParams = LinearLayout.LayoutParams(
					ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT)
			}
			val root = LinearLayout(activity).apply {
				orientation = LinearLayout.VERTICAL
				gravity = Gravity.CENTER_HORIZONTAL
				setPadding(dp(6), dp(2), dp(6), dp(4))
				addView(imageWrap)
				addView(name)
			}
			return Holder(root, image, name, info)
		}

		override fun onBindViewHolder(holder: Holder, position: Int) {
			val e = items[position]
			holder.image.setImageBitmap(ThumbnailCache.get(activity.filesDir, e.key, dp(110)))
			holder.name.text = e.name
			holder.info.setOnClickListener {
				it.animate().scaleX(1.3f).scaleY(1.3f).setDuration(90L)
					.withEndAction { it.animate().scaleX(1f).scaleY(1f).setDuration(120L).start() }.start()
				showInfo(e)
			}
			var dragging = false
			holder.root.setOnLongClickListener { v ->
				dragging = true
				v.parent?.requestDisallowInterceptTouchEvent(true)
				v.performHapticFeedback(android.view.HapticFeedbackConstants.LONG_PRESS)
				false // keep receiving the touch stream below
			}
			holder.root.setOnTouchListener { v, ev ->
				when (ev.actionMasked) {
					MotionEvent.ACTION_MOVE -> if (dragging) {
						if (ghost == null)
							showGhost(e.key, ev.rawX, ev.rawY)
						else
							moveGhost(ev.rawX, ev.rawY)
					}
					MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
						if (dragging) {
							dragging = false
							val loc = IntArray(2)
							(popup?.contentView ?: v).getLocationOnScreen(loc)
							// Module top-left lands at the ghost's top-left (aim point).
							if (ev.actionMasked == MotionEvent.ACTION_UP && ev.rawY < loc[1])
								chooseAt(e.key, ev.rawX - ghostW / 2f, ev.rawY - ghostH / 2f)
							hideGhost()
						}
					}
				}
				false // let clicks and RecyclerView scrolling work normally
			}
			holder.root.setOnClickListener {
				it.animate().scaleX(0.88f).scaleY(0.88f).setDuration(90L)
					.withEndAction { it.animate().scaleX(1f).scaleY(1f).setDuration(140L)
						.setInterpolator(android.view.animation.OvershootInterpolator()).start() }
					.start()
				// Quick tap: place at the center of the screen.
				val dm = activity.resources.displayMetrics
				chooseAt(e.key, dm.widthPixels / 2f, dm.heightPixels / 2.4f)
			}
		}

		override fun getItemCount() = items.size
	}
}
