package org.rackdroid

import android.app.Activity
import android.app.Dialog
import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.PorterDuff
import android.graphics.PorterDuffXfermode
import android.graphics.Rect
import android.graphics.RectF
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.widget.FrameLayout
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.PopupWindow
import android.widget.ScrollView
import android.widget.TextView
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat

/** In-app help surfaces: a library of topic guides (Help ▸ Guide) and two
 * step-by-step wizards (basic + Pro), reached via the synthetic Help rows
 * appended by menu_native.cpp. The basic wizard also auto-shows once on
 * first run. Same glass styling as the menu sheets.
 *
 * Explanatory images reuse the module browser thumbnails already extracted
 * to filesDir/thumbnails/<Plugin>/<Model>.png — real renders of the exact
 * modules each step talks about, at zero extra APK weight. */

private fun rowOfModules(activity: Activity, keys: List<String>, heightDp: Int): View? {
	if (keys.isEmpty())
		return null
	val density = activity.resources.displayMetrics.density
	fun dp(v: Int) = (v * density).toInt()
	val row = LinearLayout(activity).apply {
		orientation = LinearLayout.HORIZONTAL
		gravity = Gravity.CENTER
	}
	var added = 0
	for ((i, key) in keys.withIndex()) {
		val bmp = ThumbnailCache.get(activity.filesDir, key, dp(heightDp)) ?: continue
		if (i > 0 && added > 0) {
			row.addView(TextView(activity).apply {
				text = "▸"
				textSize = 22f
				setTextColor(AppTheme.current.accent)
				setPadding(dp(8), 0, dp(8), 0)
			})
		}
		row.addView(ImageView(activity).apply {
			setImageBitmap(bmp)
			adjustViewBounds = true
			background = GradientDrawable().apply {
				cornerRadius = dp(6).toFloat()
				setStroke(dp(1), AppTheme.withAlpha(Color.WHITE, 12))
			}
			clipToOutline = true
			layoutParams = LinearLayout.LayoutParams(
				ViewGroup.LayoutParams.WRAP_CONTENT, dp(heightDp))
		})
		added++
	}
	return if (added > 0) row else null
}


// ---- Guide library ----------------------------------------------------------

data class GuideSection(val t: Int, val b: Int, val images: List<String> = emptyList())
data class GuideTopic(val icon: String, val title: Int, val subtitle: Int, val sections: List<GuideSection>)

val GUIDE_TOPICS = listOf(
	GuideTopic("🚀", R.string.guide_t1_title, R.string.guide_t1_sub, listOf(
		GuideSection(R.string.guide_s1_t, R.string.guide_s1_b),
		GuideSection(R.string.guide_s2_t, R.string.guide_s2_b),
		GuideSection(R.string.guide_s3_t, R.string.guide_s3_b),
		GuideSection(R.string.guide_s4_t, R.string.guide_s4_b),
		GuideSection(R.string.guide_s5_t, R.string.guide_s5_b),
		GuideSection(R.string.guide_s6_t, R.string.guide_s6_b),
		GuideSection(R.string.guide_s7_t, R.string.guide_s7_b),
		GuideSection(R.string.guide_s8_t, R.string.guide_s8_b),
		GuideSection(R.string.guide_s9_t, R.string.guide_s9_b),
		GuideSection(R.string.guide_s10_t, R.string.guide_s10_b),
	)),
	GuideTopic("🔍", R.string.guide_t2_title, R.string.guide_t2_sub, listOf(
		GuideSection(R.string.guide_t2_s1_t, R.string.guide_t2_s1_b),
		GuideSection(R.string.guide_t2_s2_t, R.string.guide_t2_s2_b),
		GuideSection(R.string.guide_t2_s3_t, R.string.guide_t2_s3_b),
	)),
	GuideTopic("🔌", R.string.guide_t3_title, R.string.guide_t3_sub, listOf(
		GuideSection(R.string.guide_t3_s1_t, R.string.guide_t3_s1_b),
		GuideSection(R.string.guide_t3_s2_t, R.string.guide_t3_s2_b),
		GuideSection(R.string.guide_t3_s3_t, R.string.guide_t3_s3_b),
		GuideSection(R.string.guide_t3_s4_t, R.string.guide_t3_s4_b),
		GuideSection(R.string.guide_t3_s5_t, R.string.guide_t3_s5_b),
	)),
	GuideTopic("🌊", R.string.guide_t4_title, R.string.guide_t4_sub, listOf(
		GuideSection(R.string.guide_t4_s1_t, R.string.guide_t4_s1_b,
			listOf("Fundamental/VCO", "Fundamental/LFO")),
		GuideSection(R.string.guide_t4_s2_t, R.string.guide_t4_s2_b),
		GuideSection(R.string.guide_t4_s3_t, R.string.guide_t4_s3_b),
	)),
	GuideTopic("🎚", R.string.guide_t5_title, R.string.guide_t5_sub, listOf(
		GuideSection(R.string.guide_t5_s1_t, R.string.guide_t5_s1_b,
			listOf("Fundamental/VCF")),
		GuideSection(R.string.guide_t5_s2_t, R.string.guide_t5_s2_b),
		GuideSection(R.string.guide_t5_s3_t, R.string.guide_t5_s3_b),
	)),
	GuideTopic("📈", R.string.guide_t6_title, R.string.guide_t6_sub, listOf(
		GuideSection(R.string.guide_t6_s1_t, R.string.guide_t6_s1_b,
			listOf("Fundamental/ADSR", "Fundamental/VCA")),
		GuideSection(R.string.guide_t6_s2_t, R.string.guide_t6_s2_b),
		GuideSection(R.string.guide_t6_s3_t, R.string.guide_t6_s3_b),
	)),
	GuideTopic("🥁", R.string.guide_t7_title, R.string.guide_t7_sub, listOf(
		GuideSection(R.string.guide_t7_s1_t, R.string.guide_t7_s1_b,
			listOf("RackDroidDrums/BD909", "RackDroidDrums/SD808", "RackDroidDrums/HH606")),
		GuideSection(R.string.guide_t7_s2_t, R.string.guide_t7_s2_b),
		GuideSection(R.string.guide_t7_s3_t, R.string.guide_t7_s3_b),
	)),
	GuideTopic("🎹", R.string.guide_t8_title, R.string.guide_t8_sub, listOf(
		GuideSection(R.string.guide_t8_s1_t, R.string.guide_t8_s1_b,
			listOf("Core/MIDIToCVInterface")),
		GuideSection(R.string.guide_t8_s2_t, R.string.guide_t8_s2_b),
		GuideSection(R.string.guide_t8_s3_t, R.string.guide_t8_s3_b),
	)),
	GuideTopic("🎛", R.string.guide_t9_title, R.string.guide_t9_sub, listOf(
		GuideSection(R.string.guide_t9_s1_t, R.string.guide_t9_s1_b,
			listOf("Fundamental/VCMixer", "Core/AudioInterface2")),
		GuideSection(R.string.guide_t9_s2_t, R.string.guide_t9_s2_b),
		GuideSection(R.string.guide_t9_s3_t, R.string.guide_t9_s3_b),
	)),
	GuideTopic("⚡", R.string.guide_t10_title, R.string.guide_t10_sub, listOf(
		GuideSection(R.string.guide_t10_s1_t, R.string.guide_t10_s1_b),
		GuideSection(R.string.guide_t10_s2_t, R.string.guide_t10_s2_b),
		GuideSection(R.string.guide_t10_s3_t, R.string.guide_t10_s3_b),
	)),
)


/** Staggered entrance for a column of views: each child drifts up and
 * fades in slightly after the previous one. */
private fun staggerIn(parent: ViewGroup, stepMs: Long = 24L) {
	val density = parent.resources.displayMetrics.density
	for (i in 0 until parent.childCount) {
		val v = parent.getChildAt(i)
		v.alpha = 0f
		v.translationY = 20f * density
		v.animate().alpha(1f).translationY(0f)
			.setStartDelay(i * stepMs).setDuration(220L)
			.setInterpolator(android.view.animation.DecelerateInterpolator()).start()
	}
}

private fun glassDialog(activity: Activity, content: View): Dialog {
	val blurOn = crossWindowBlurEnabled(activity.windowManager)
	val dialog = Dialog(activity, android.R.style.Theme_Black_NoTitleBar_Fullscreen)
	dialog.setContentView(ScrollView(activity).apply {
		setBackgroundColor(if (blurOn) AppTheme.withAlpha(AppTheme.current.surface, 88) else AppTheme.current.surface)
		addView(content)
	})
	dialog.window?.setBackgroundDrawable(
		android.graphics.drawable.ColorDrawable(Color.TRANSPARENT))
	dialog.window?.attributes = dialog.window?.attributes?.apply {
		windowAnimations = android.R.style.Animation_Dialog
	}
	(activity as? MainActivity)?.glassify(dialog.window, 56)
	(activity as? MainActivity)?.trackTopWindow(dialog)
	dialog.setOnShowListener { (content as? ViewGroup)?.let { staggerIn(it) } }
	return dialog
}


/** Topic index: one tappable glass card per guide. */
class GuideSheet(private val activity: Activity) {
	private fun dp(v: Int) = (v * activity.resources.displayMetrics.density).toInt()

	fun show() {
		val font = AppFont.get(activity)
		val content = LinearLayout(activity).apply {
			orientation = LinearLayout.VERTICAL
			setPadding(dp(20), dp(16), dp(20), dp(28))
		}
		content.addView(TextView(activity).apply {
			text = activity.getString(R.string.guide_title)
			setTextColor(AppTheme.current.accent)
			textSize = 22f
			setTypeface(font, Typeface.BOLD)
			setPadding(dp(4), 0, 0, dp(6))
		})
		var dialog: Dialog? = null
		// Replay the first-run interface tour (doneFlag = null: does not touch
		// the once-only flag, so it can be run again and again).
		content.addView(LinearLayout(activity).apply {
			orientation = LinearLayout.HORIZONTAL
			gravity = Gravity.CENTER_VERTICAL
			background = GradientDrawable().apply {
				cornerRadius = dp(16).toFloat()
				setColor(AppTheme.current.surfaceInset)
				setStroke(dp(1), AppTheme.withAlpha(AppTheme.current.accent, 40))
			}
			setPadding(dp(16), dp(14), dp(16), dp(14))
			layoutParams = LinearLayout.LayoutParams(
				ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT
			).apply { topMargin = dp(10) }
			addView(TextView(activity).apply { text = "🧭"; textSize = 24f; setPadding(0, 0, dp(16), 0) })
			addView(TextView(activity).apply {
				text = activity.getString(R.string.tour_replay)
				setTextColor(AppTheme.current.textPrimary); textSize = 16f
				setTypeface(font, Typeface.BOLD)
			}, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
			addView(TextView(activity).apply {
				text = "›"; textSize = 20f; setTextColor(AppTheme.current.textDisabled)
			})
			setOnClickListener { dialog?.dismiss(); Tour(activity, doneFlag = null).show() }
		})
		for (topic in GUIDE_TOPICS) {
			val card = LinearLayout(activity).apply {
				orientation = LinearLayout.HORIZONTAL
				gravity = Gravity.CENTER_VERTICAL
				background = GradientDrawable().apply {
					cornerRadius = dp(16).toFloat()
					setColor(AppTheme.current.surfaceInset)
					setStroke(dp(1), AppTheme.withAlpha(Color.WHITE, 9))
				}
				setPadding(dp(16), dp(14), dp(16), dp(14))
				layoutParams = LinearLayout.LayoutParams(
					ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT
				).apply { topMargin = dp(10) }
			}
			card.addView(TextView(activity).apply {
				text = topic.icon
				textSize = 24f
				setPadding(0, 0, dp(16), 0)
			})
			val texts = LinearLayout(activity).apply { orientation = LinearLayout.VERTICAL }
			texts.addView(TextView(activity).apply {
				text = activity.getString(topic.title)
				setTextColor(AppTheme.current.textPrimary)
				textSize = 16f
				setTypeface(font, Typeface.BOLD)
			})
			texts.addView(TextView(activity).apply {
				text = activity.getString(topic.subtitle)
				setTextColor(AppTheme.current.textSecondary)
				textSize = 12f
				typeface = font
			})
			card.addView(texts, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
			card.addView(TextView(activity).apply {
				text = "›"
				textSize = 20f
				setTextColor(AppTheme.current.textDisabled)
			})
			card.setOnClickListener { GuideTopicSheet(activity, topic).show() }
			content.addView(card)
		}
		val dlg = glassDialog(activity, content)
		dialog = dlg
		dlg.show()
	}
}


/** One topic: scrollable titled sections, with module images where they help. */
class GuideTopicSheet(private val activity: Activity, private val topic: GuideTopic) {
	private fun dp(v: Int) = (v * activity.resources.displayMetrics.density).toInt()

	fun show() {
		val font = AppFont.get(activity)
		val content = LinearLayout(activity).apply {
			orientation = LinearLayout.VERTICAL
			setPadding(dp(24), dp(16), dp(24), dp(28))
		}
		content.addView(TextView(activity).apply {
			text = "${topic.icon}  ${activity.getString(topic.title)}"
			setTextColor(AppTheme.current.accent)
			textSize = 21f
			setTypeface(font, Typeface.BOLD)
		})
		for (s in topic.sections) {
			content.addView(TextView(activity).apply {
				text = activity.getString(s.t)
				setTextColor(AppTheme.current.textPrimary)
				textSize = 16f
				setTypeface(font, Typeface.BOLD)
				setPadding(0, dp(18), 0, 0)
			})
			content.addView(TextView(activity).apply {
				text = activity.getString(s.b)
				setTextColor(AppTheme.current.textPrimary)
				textSize = 14f
				typeface = font
				setPadding(0, dp(4), 0, 0)
			})
			rowOfModules(activity, s.images, 150)?.let {
				it.setPadding(0, dp(10), 0, 0)
				content.addView(it)
			}
		}
		glassDialog(activity, content).show()
	}
}




// ---- Tutorial library ---------------------------------------------------------

data class Tutorial(val icon: String, val title: Int, val sub: Int, val level: Int, val steps: List<WizardStep>)

val TUTORIALS = listOf(
	Tutorial("🎵", R.string.wizard_title, R.string.tut_basic_sub, 1, Wizard.BASIC_STEPS),
	Tutorial("🧠", R.string.wizard_pro_title, R.string.tut_pro_sub, 1, Wizard.PRO_STEPS),
	Tutorial("🎚", R.string.t03_title, R.string.t03_sub, 1, listOf(
			WizardStep(R.string.t03_s0, listOf("Fundamental/VCMixer")),
			WizardStep(R.string.t03_s1, listOf("Fundamental/VCO", "Fundamental/VCO")),
			WizardStep(R.string.t03_s2, listOf("Fundamental/VCO", "Fundamental/VCMixer")),
			WizardStep(R.string.t03_s3, listOf("Fundamental/VCMixer", "Core/AudioInterface2")),
			WizardStep(R.string.t03_s4, listOf("Fundamental/VCMixer")),
			WizardStep(R.string.t03_s5),
		)),
	Tutorial("📺", R.string.t04_title, R.string.t04_sub, 1, listOf(
			WizardStep(R.string.t04_s0, listOf("Fundamental/Scope")),
			WizardStep(R.string.t04_s1, listOf("Fundamental/VCO", "Fundamental/Scope")),
			WizardStep(R.string.t04_s2, listOf("Fundamental/VCO")),
			WizardStep(R.string.t04_s3, listOf("Fundamental/LFO", "Fundamental/Scope")),
			WizardStep(R.string.t04_s4),
		)),
	Tutorial("💾", R.string.t05_title, R.string.t05_sub, 1, listOf(
			WizardStep(R.string.t05_s0),
			WizardStep(R.string.t05_s1),
			WizardStep(R.string.t05_s2),
			WizardStep(R.string.t05_s3),
			WizardStep(R.string.t05_s4),
		)),
	Tutorial("🎻", R.string.t06_title, R.string.t06_sub, 2, listOf(
			WizardStep(R.string.t06_s0, listOf("Fundamental/VCO")),
			WizardStep(R.string.t06_s1, listOf("Fundamental/LFO")),
			WizardStep(R.string.t06_s2, listOf("Fundamental/LFO", "Fundamental/VCO")),
			WizardStep(R.string.t06_s3, listOf("Fundamental/VCO")),
			WizardStep(R.string.t06_s4, listOf("Fundamental/LFO")),
			WizardStep(R.string.t06_s5),
		)),
	Tutorial("🌗", R.string.t07_title, R.string.t07_sub, 2, listOf(
			WizardStep(R.string.t07_s0, listOf("Fundamental/VCA", "Core/AudioInterface2")),
			WizardStep(R.string.t07_s1, listOf("Fundamental/LFO", "Fundamental/VCA")),
			WizardStep(R.string.t07_s2, listOf("Fundamental/VCA")),
			WizardStep(R.string.t07_s3, listOf("Fundamental/LFO")),
			WizardStep(R.string.t07_s4, listOf("Fundamental/LFO")),
		)),
	Tutorial("📐", R.string.t08_title, R.string.t08_sub, 2, listOf(
			WizardStep(R.string.t08_s0, listOf("Fundamental/VCO")),
			WizardStep(R.string.t08_s1, listOf("Fundamental/VCO")),
			WizardStep(R.string.t08_s2, listOf("Fundamental/LFO", "Fundamental/VCO")),
			WizardStep(R.string.t08_s3, listOf("Fundamental/LFO")),
			WizardStep(R.string.t08_s4),
		)),
	Tutorial("🌊", R.string.t09_title, R.string.t09_sub, 2, listOf(
			WizardStep(R.string.t09_s0, listOf("Fundamental/VCO", "Fundamental/VCF")),
			WizardStep(R.string.t09_s1, listOf("Fundamental/LFO", "Fundamental/VCF")),
			WizardStep(R.string.t09_s2, listOf("Fundamental/VCF")),
			WizardStep(R.string.t09_s3, listOf("Fundamental/VCF")),
			WizardStep(R.string.t09_s4, listOf("Fundamental/LFO")),
		)),
	Tutorial("🎲", R.string.t10_title, R.string.t10_sub, 2, listOf(
			WizardStep(R.string.t10_s0, listOf("Fundamental/Noise", "Fundamental/SHASR")),
			WizardStep(R.string.t10_s1, listOf("Fundamental/Noise", "Fundamental/SHASR")),
			WizardStep(R.string.t10_s2, listOf("Fundamental/LFO", "Fundamental/SHASR")),
			WizardStep(R.string.t10_s3, listOf("Fundamental/SHASR", "Fundamental/VCO")),
			WizardStep(R.string.t10_s4, listOf("Fundamental/LFO")),
			WizardStep(R.string.t10_s5),
		)),
	Tutorial("🎼", R.string.t11_title, R.string.t11_sub, 2, listOf(
			WizardStep(R.string.t11_s0, listOf("Fundamental/Quantizer")),
			WizardStep(R.string.t11_s1, listOf("Fundamental/SHASR", "Fundamental/Quantizer")),
			WizardStep(R.string.t11_s2, listOf("Fundamental/Quantizer")),
			WizardStep(R.string.t11_s3),
			WizardStep(R.string.t11_s4),
		)),
	Tutorial("🥁", R.string.t12_title, R.string.t12_sub, 3, listOf(
			WizardStep(R.string.t12_s0, listOf("RackDroidDrums/BD808", "RackDroidDrums/SD808")),
			WizardStep(R.string.t12_s1, listOf("Fundamental/SEQ3", "RackDroidDrums/BD808")),
			WizardStep(R.string.t12_s2, listOf("Fundamental/SEQ3", "RackDroidDrums/SD808")),
			WizardStep(R.string.t12_s3, listOf("Fundamental/VCMixer", "Core/AudioInterface2")),
			WizardStep(R.string.t12_s4, listOf("Fundamental/SEQ3")),
			WizardStep(R.string.t12_s5, listOf("RackDroidDrums/BD808", "RackDroidDrums/SD808")),
		)),
	Tutorial("🎩", R.string.t13_title, R.string.t13_sub, 3, listOf(
			WizardStep(R.string.t13_s0, listOf("RackDroidDrums/HH808")),
			WizardStep(R.string.t13_s1, listOf("Fundamental/SEQ3", "RackDroidDrums/HH808")),
			WizardStep(R.string.t13_s2, listOf("RackDroidDrums/HH808")),
			WizardStep(R.string.t13_s3),
			WizardStep(R.string.t13_s4, listOf("RackDroidDrums/HH808")),
			WizardStep(R.string.t13_s5, listOf("Fundamental/VCMixer")),
		)),
	Tutorial("🧪", R.string.t14_title, R.string.t14_sub, 3, listOf(
			WizardStep(R.string.t14_s0, listOf("Fundamental/SEQ3", "Fundamental/VCO")),
			WizardStep(R.string.t14_s1, listOf("Fundamental/ADSR", "Fundamental/VCA")),
			WizardStep(R.string.t14_s2, listOf("Fundamental/SEQ3")),
			WizardStep(R.string.t14_s3, listOf("Fundamental/VCF")),
			WizardStep(R.string.t14_s4, listOf("Fundamental/VCF")),
			WizardStep(R.string.t14_s5),
		)),
	Tutorial("⏱", R.string.t15_title, R.string.t15_sub, 3, listOf(
			WizardStep(R.string.t15_s0, listOf("Fundamental/LFO", "Fundamental/SEQ3")),
			WizardStep(R.string.t15_s1, listOf("Fundamental/LFO")),
			WizardStep(R.string.t15_s2, listOf("Fundamental/SEQ3")),
			WizardStep(R.string.t15_s3, listOf("Fundamental/SEQ3")),
			WizardStep(R.string.t15_s4),
		)),
	Tutorial("🔀", R.string.t16_title, R.string.t16_sub, 3, listOf(
			WizardStep(R.string.t16_s0, listOf("Fundamental/SEQ3")),
			WizardStep(R.string.t16_s1),
			WizardStep(R.string.t16_s2, listOf("Fundamental/LFO", "Fundamental/SEQ3")),
			WizardStep(R.string.t16_s3),
			WizardStep(R.string.t16_s4, listOf("Fundamental/SEQ3")),
		)),
	Tutorial("🎛", R.string.t17_title, R.string.t17_sub, 3, listOf(
			WizardStep(R.string.t17_s0, listOf("RackDroidDrums/BD909", "RackDroidDrums/SD909", "RackDroidDrums/HH909")),
			WizardStep(R.string.t17_s1, listOf("Fundamental/SEQ3")),
			WizardStep(R.string.t17_s2, listOf("Fundamental/VCMixer", "Core/AudioInterface2")),
			WizardStep(R.string.t17_s3, listOf("Fundamental/VCMixer")),
			WizardStep(R.string.t17_s4, listOf("RackDroidDrums/BD909", "RackDroidDrums/SD909")),
			WizardStep(R.string.t17_s5),
			WizardStep(R.string.t17_s6),
		)),
	Tutorial("🔁", R.string.t18_title, R.string.t18_sub, 4, listOf(
			WizardStep(R.string.t18_s0, listOf("Fundamental/Delay", "Core/AudioInterface2")),
			WizardStep(R.string.t18_s1, listOf("Fundamental/Delay")),
			WizardStep(R.string.t18_s2),
			WizardStep(R.string.t18_s3, listOf("Fundamental/Delay")),
			WizardStep(R.string.t18_s4),
			WizardStep(R.string.t18_s5),
		)),
	Tutorial("🏔", R.string.t19_title, R.string.t19_sub, 4, listOf(
			WizardStep(R.string.t19_s0, listOf("Fundamental/Delay", "Core/AudioInterface2")),
			WizardStep(R.string.t19_s1, listOf("Fundamental/Delay")),
			WizardStep(R.string.t19_s2),
			WizardStep(R.string.t19_s3, listOf("Fundamental/Delay", "Fundamental/Delay")),
			WizardStep(R.string.t19_s4),
		)),
	Tutorial("🌀", R.string.t20_title, R.string.t20_sub, 4, listOf(
			WizardStep(R.string.t20_s0, listOf("Fundamental/Delay")),
			WizardStep(R.string.t20_s1, listOf("Fundamental/LFO", "Fundamental/Delay")),
			WizardStep(R.string.t20_s2),
			WizardStep(R.string.t20_s3, listOf("Fundamental/Delay")),
			WizardStep(R.string.t20_s4),
		)),
	Tutorial("💫", R.string.t21_title, R.string.t21_sub, 4, listOf(
			WizardStep(R.string.t21_s0, listOf("Fundamental/Delay")),
			WizardStep(R.string.t21_s1, listOf("Fundamental/LFO", "Fundamental/Delay")),
			WizardStep(R.string.t21_s2),
			WizardStep(R.string.t21_s3, listOf("Fundamental/Delay")),
			WizardStep(R.string.t21_s4),
			WizardStep(R.string.t21_s5),
		)),
	Tutorial("🔥", R.string.t22_title, R.string.t22_sub, 4, listOf(
			WizardStep(R.string.t22_s0, listOf("Fundamental/VCF")),
			WizardStep(R.string.t22_s1, listOf("Fundamental/VCA")),
			WizardStep(R.string.t22_s2, listOf("Fundamental/VCF")),
			WizardStep(R.string.t22_s3, listOf("RackDroidDrums/BD808")),
			WizardStep(R.string.t22_s4),
		)),
	Tutorial("🎧", R.string.t23_title, R.string.t23_sub, 4, listOf(
			WizardStep(R.string.t23_s0, listOf("Fundamental/VCA", "Core/AudioInterface2")),
			WizardStep(R.string.t23_s1, listOf("Fundamental/LFO", "Fundamental/VCA")),
			WizardStep(R.string.t23_s2, listOf("Fundamental/8vert", "Fundamental/VCA")),
			WizardStep(R.string.t23_s3),
			WizardStep(R.string.t23_s4, listOf("Fundamental/LFO")),
			WizardStep(R.string.t23_s5, listOf("Fundamental/8vert")),
		)),
	Tutorial("🦆", R.string.t24_title, R.string.t24_sub, 4, listOf(
			WizardStep(R.string.t24_s0, listOf("Fundamental/VCA")),
			WizardStep(R.string.t24_s1, listOf("Fundamental/ADSR")),
			WizardStep(R.string.t24_s2, listOf("Fundamental/8vert", "Fundamental/VCA")),
			WizardStep(R.string.t24_s3),
			WizardStep(R.string.t24_s4),
			WizardStep(R.string.t24_s5),
		)),
	Tutorial("👯", R.string.t25_title, R.string.t25_sub, 5, listOf(
			WizardStep(R.string.t25_s0, listOf("Fundamental/VCO", "Fundamental/VCO")),
			WizardStep(R.string.t25_s1, listOf("Fundamental/VCMixer", "Fundamental/VCF")),
			WizardStep(R.string.t25_s2, listOf("Fundamental/VCO")),
			WizardStep(R.string.t25_s3),
			WizardStep(R.string.t25_s4, listOf("Fundamental/Octave", "Fundamental/VCO")),
			WizardStep(R.string.t25_s5),
		)),
	Tutorial("🔔", R.string.t26_title, R.string.t26_sub, 5, listOf(
			WizardStep(R.string.t26_s0, listOf("Fundamental/VCO", "Fundamental/VCO")),
			WizardStep(R.string.t26_s1, listOf("Fundamental/VCO")),
			WizardStep(R.string.t26_s2),
			WizardStep(R.string.t26_s3, listOf("Fundamental/VCO")),
			WizardStep(R.string.t26_s4, listOf("Fundamental/ADSR", "Fundamental/VCA")),
			WizardStep(R.string.t26_s5),
		)),
	Tutorial("🔺", R.string.t27_title, R.string.t27_sub, 5, listOf(
			WizardStep(R.string.t27_s0, listOf("Fundamental/VCO", "Fundamental/VCO")),
			WizardStep(R.string.t27_s1, listOf("Fundamental/VCO", "Core/AudioInterface2")),
			WizardStep(R.string.t27_s2, listOf("Fundamental/VCO")),
			WizardStep(R.string.t27_s3, listOf("Fundamental/VCO")),
			WizardStep(R.string.t27_s4, listOf("Fundamental/LFO", "Fundamental/VCO")),
			WizardStep(R.string.t27_s5),
		)),
	Tutorial("🎸", R.string.t28_title, R.string.t28_sub, 5, listOf(
			WizardStep(R.string.t28_s0, listOf("Fundamental/Noise", "Fundamental/Delay")),
			WizardStep(R.string.t28_s1, listOf("Fundamental/Noise", "Core/AudioInterface2")),
			WizardStep(R.string.t28_s2, listOf("Fundamental/Delay")),
			WizardStep(R.string.t28_s3, listOf("Fundamental/VCF", "Fundamental/Delay")),
			WizardStep(R.string.t28_s4, listOf("Fundamental/ADSR", "Fundamental/VCA")),
			WizardStep(R.string.t28_s5, listOf("Fundamental/SEQ3")),
		)),
	Tutorial("☁", R.string.t29_title, R.string.t29_sub, 5, listOf(
			WizardStep(R.string.t29_s0, listOf("Fundamental/VCO")),
			WizardStep(R.string.t29_s1, listOf("Fundamental/LFO", "Fundamental/SHASR")),
			WizardStep(R.string.t29_s2, listOf("Fundamental/SHASR", "Fundamental/Quantizer")),
			WizardStep(R.string.t29_s3, listOf("Fundamental/LFO", "Fundamental/VCF")),
			WizardStep(R.string.t29_s4),
			WizardStep(R.string.t29_s5),
		)),
	Tutorial("🌌", R.string.t30_title, R.string.t30_sub, 5, listOf(
			WizardStep(R.string.t30_s0, listOf("Fundamental/VCO", "Fundamental/Delay")),
			WizardStep(R.string.t30_s1, listOf("Fundamental/LFO", "Fundamental/VCF")),
			WizardStep(R.string.t30_s2, listOf("Fundamental/LFO", "Fundamental/VCO")),
			WizardStep(R.string.t30_s3, listOf("Fundamental/Delay")),
			WizardStep(R.string.t30_s4),
			WizardStep(R.string.t30_s5),
			WizardStep(R.string.t30_s6),
		)),
)

/** Tutorial index: 30 step-by-step builds grouped by level; tapping one
 * closes the index and floats its wizard card over the rack. */
class TutorialLibrarySheet(private val activity: Activity) {
	private fun dp(v: Int) = (v * activity.resources.displayMetrics.density).toInt()

	fun show() {
		val font = AppFont.get(activity)
		val content = LinearLayout(activity).apply {
			orientation = LinearLayout.VERTICAL
			setPadding(dp(20), dp(16), dp(20), dp(28))
		}
		content.addView(TextView(activity).apply {
			text = activity.getString(R.string.tut_library_title)
			setTextColor(AppTheme.current.accent)
			textSize = 22f
			setTypeface(font, Typeface.BOLD)
			setPadding(dp(4), 0, 0, dp(2))
		})
		val dialog = glassDialog(activity, content)
		var lastLevel = 0
		for (t in TUTORIALS) {
			if (t.level != lastLevel) {
				lastLevel = t.level
				val levelRes = when (t.level) {
					1 -> R.string.tutlevel_1; 2 -> R.string.tutlevel_2
					3 -> R.string.tutlevel_3; 4 -> R.string.tutlevel_4
					else -> R.string.tutlevel_5
				}
				content.addView(TextView(activity).apply {
					text = activity.getString(levelRes).uppercase()
					setTextColor(Color.parseColor("#C8985C"))
					textSize = 12f
					letterSpacing = 0.08f
					setTypeface(font, Typeface.BOLD)
					setPadding(dp(4), dp(18), 0, dp(2))
				})
			}
			val card = LinearLayout(activity).apply {
				orientation = LinearLayout.HORIZONTAL
				gravity = Gravity.CENTER_VERTICAL
				background = GradientDrawable().apply {
					cornerRadius = dp(16).toFloat()
					setColor(AppTheme.current.surfaceInset)
					setStroke(dp(1), AppTheme.withAlpha(Color.WHITE, 9))
				}
				setPadding(dp(16), dp(12), dp(16), dp(12))
				layoutParams = LinearLayout.LayoutParams(
					ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT
				).apply { topMargin = dp(8) }
			}
			card.addView(TextView(activity).apply {
				text = t.icon
				textSize = 22f
				setPadding(0, 0, dp(14), 0)
			})
			val texts = LinearLayout(activity).apply { orientation = LinearLayout.VERTICAL }
			texts.addView(TextView(activity).apply {
				text = activity.getString(t.title)
				setTextColor(AppTheme.current.textPrimary)
				textSize = 15f
				setTypeface(font, Typeface.BOLD)
			})
			texts.addView(TextView(activity).apply {
				text = activity.getString(t.sub) + "  ·  " +
					activity.getString(R.string.tut_steps_count, t.steps.size)
				setTextColor(AppTheme.current.textSecondary)
				textSize = 12f
				typeface = font
			})
			card.addView(texts, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
			card.addView(TextView(activity).apply {
				text = "›"
				textSize = 20f
				setTextColor(AppTheme.current.textDisabled)
			})
			card.setOnClickListener {
				dialog.dismiss()
				(activity as? MainActivity)?.startTutorial(t)
			}
			content.addView(card)
		}
		dialog.show()
	}
}


// ---- Wizards ----------------------------------------------------------------

data class WizardStep(val text: Int, val images: List<String> = emptyList())

/** Floating step-by-step tutorial: a small glass card anchored to the
 * bottom; the rack stays fully interactive so the user performs each step
 * for real, then taps Next. Steps can carry explanatory module images
 * (thumbnails of the exact modules involved, with ▸ between connection
 * endpoints). */
class Wizard(
	private val activity: Activity,
	private val titleRes: Int = R.string.wizard_title,
	private val steps: List<WizardStep> = BASIC_STEPS,
	/** SharedPreferences flag set on dismissal (suppresses first-run
	 * auto-show); null for wizards that never auto-show (the Pro one). */
	private val doneFlag: String? = "wizard_done",
) {
	companion object {
		private const val VCO = "Fundamental/VCO"
		private const val VCF = "Fundamental/VCF"
		private const val VCA = "Fundamental/VCA"
		private const val ADSR = "Fundamental/ADSR"
		private const val LFO = "Fundamental/LFO"
		private const val SEQ3 = "Fundamental/SEQ3"
		private const val SCOPE = "Fundamental/Scope"
		private const val AUDIO2 = "Core/AudioInterface2"

		val BASIC_STEPS = listOf(
			WizardStep(R.string.wizard_s0),
			WizardStep(R.string.wizard_s1),
			WizardStep(R.string.wizard_s2, listOf(VCO)),
			WizardStep(R.string.wizard_s3, listOf(AUDIO2)),
			WizardStep(R.string.wizard_s4, listOf(VCO, AUDIO2)),
			WizardStep(R.string.wizard_s5, listOf(VCO)),
			WizardStep(R.string.wizard_s6, listOf(AUDIO2)),
			WizardStep(R.string.wizard_s7),
		)
		/** Tutorial Pro: a full subtractive-synth voice, one concept per
		 * step, explaining what each module does and WHY it is patched
		 * where it is. */
		val PRO_STEPS = listOf(
			WizardStep(R.string.wizard_pro_s00),
			WizardStep(R.string.wizard_pro_s01),
			WizardStep(R.string.wizard_pro_s02),
			WizardStep(R.string.wizard_pro_s03, listOf(VCO)),
			WizardStep(R.string.wizard_pro_s04, listOf(VCO)),
			WizardStep(R.string.wizard_pro_s05, listOf(VCF)),
			WizardStep(R.string.wizard_pro_s06, listOf(VCO, VCF)),
			WizardStep(R.string.wizard_pro_s07, listOf(VCF, AUDIO2)),
			WizardStep(R.string.wizard_pro_s08, listOf(VCF)),
			WizardStep(R.string.wizard_pro_s09, listOf(VCF)),
			WizardStep(R.string.wizard_pro_s10),
			WizardStep(R.string.wizard_pro_s11, listOf(VCA)),
			WizardStep(R.string.wizard_pro_s12, listOf(VCF, VCA)),
			WizardStep(R.string.wizard_pro_s13, listOf(VCA, AUDIO2)),
			WizardStep(R.string.wizard_pro_s14, listOf(ADSR)),
			WizardStep(R.string.wizard_pro_s15, listOf(ADSR, VCA)),
			WizardStep(R.string.wizard_pro_s16, listOf(ADSR)),
			WizardStep(R.string.wizard_pro_s17, listOf(SEQ3)),
			WizardStep(R.string.wizard_pro_s18, listOf(SEQ3, ADSR)),
			WizardStep(R.string.wizard_pro_s19, listOf(SEQ3, VCO)),
			WizardStep(R.string.wizard_pro_s20, listOf(SEQ3)),
			WizardStep(R.string.wizard_pro_s21, listOf(SEQ3)),
			WizardStep(R.string.wizard_pro_s22, listOf(ADSR)),
			WizardStep(R.string.wizard_pro_s23, listOf(LFO)),
			WizardStep(R.string.wizard_pro_s24, listOf(LFO, VCF)),
			WizardStep(R.string.wizard_pro_s25, listOf(VCF)),
			WizardStep(R.string.wizard_pro_s26, listOf(LFO)),
			WizardStep(R.string.wizard_pro_s27, listOf(LFO, VCO)),
			WizardStep(R.string.wizard_pro_s28, listOf(VCA, SCOPE)),
			WizardStep(R.string.wizard_pro_s29),
		)
	}

	private fun dp(v: Int) = (v * activity.resources.displayMetrics.density).toInt()
	private var step = 0
	private var popup: PopupWindow? = null
	private var cardView: View? = null
	private lateinit var stepLabel: TextView
	private lateinit var body: TextView
	private lateinit var stepContent: LinearLayout
	private lateinit var imageHolder: LinearLayout
	private lateinit var prevBtn: TextView
	private lateinit var nextBtn: TextView

	fun show() {
		if (popup != null)
			return
		// Fold the top tools card to just its glass arrow, clearing the top
		// for this tutorial card (user request).
		(activity as? MainActivity)?.setToolbarCollapsedForTutorial(true)
		val font = AppFont.get(activity)
		fun label(size: Float, color: Int, bold: Boolean = false) = TextView(activity).apply {
			textSize = size
			setTextColor(color)
			setTypeface(font, if (bold) Typeface.BOLD else Typeface.NORMAL)
		}
		val card = LinearLayout(activity).apply {
			orientation = LinearLayout.VERTICAL
			background = GradientDrawable().apply {
				cornerRadius = dp(20).toFloat()
				setColor(AppTheme.withAlpha(AppTheme.current.surface, 94))
				setStroke(dp(1), AppTheme.withAlpha(Color.WHITE, 18))
			}
			setPadding(dp(20), dp(14), dp(20), dp(12))
		}
		val header = LinearLayout(activity).apply {
			orientation = LinearLayout.HORIZONTAL
			gravity = Gravity.CENTER_VERTICAL
		}
		val title = label(15f, AppTheme.current.accent, bold = true).apply {
			text = activity.getString(titleRes)
			layoutParams = LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f)
		}
		stepLabel = label(12f, AppTheme.current.textSecondary)
		val close = label(16f, AppTheme.current.textSecondary).apply {
			text = "✕"
			setPadding(dp(14), 0, 0, 0)
			setOnClickListener { dismiss() }
		}
		header.addView(title); header.addView(stepLabel); header.addView(close)
		card.addView(header)

		stepContent = LinearLayout(activity).apply { orientation = LinearLayout.VERTICAL }
		imageHolder = LinearLayout(activity).apply {
			orientation = LinearLayout.VERTICAL
			gravity = Gravity.CENTER_HORIZONTAL
		}
		stepContent.addView(imageHolder)

		body = label(14f, AppTheme.current.textPrimary).apply { setPadding(0, dp(8), 0, dp(10)) }
		stepContent.addView(body)
		card.addView(stepContent)

		val buttons = LinearLayout(activity).apply {
			orientation = LinearLayout.HORIZONTAL
			gravity = Gravity.END
		}
		fun navButton(onTap: () -> Unit) = label(14f, AppTheme.current.accent, bold = true).apply {
			setPadding(dp(16), dp(8), dp(16), dp(8))
			setOnClickListener { onTap() }
		}
		prevBtn = navButton { if (step > 0) { step--; render(-1) } }
		nextBtn = navButton {
			if (step < steps.size - 1) { step++; render(1) } else dismiss()
		}
		buttons.addView(prevBtn); buttons.addView(nextBtn)
		card.addView(buttons)

		val p = PopupWindow(card,
			(activity.resources.displayMetrics.widthPixels * 0.94).toInt(),
			ViewGroup.LayoutParams.WRAP_CONTENT)
		p.isFocusable = false // the rack must keep receiving input
		popup = p
		cardView = card
		render(0)
		val anchor = (activity as? MainActivity)?.wizardAnchor() ?: activity.window.decorView
		anchor.post {
			runCatching {
				// TOP of the screen (below the tools card): keeps the module
				// palette at the bottom clear, since the user drags modules
				// from it while following the tutorial.
				p.showAtLocation(anchor, Gravity.TOP or Gravity.CENTER_HORIZONTAL, 0, topOffset())
				// Entrance: the card drops in from above and fades in.
				card.alpha = 0f
				card.translationY = -dp(48).toFloat()
				card.animate().alpha(1f).translationY(0f).setDuration(280L)
					.setInterpolator(android.view.animation.DecelerateInterpolator()).start()
			}
		}
	}

	/** Just below the status bar and the collapsed tools card's arrow. */
	private fun topOffset(): Int {
		val inset = ViewCompat.getRootWindowInsets(activity.window.decorView)?.getInsets(
			WindowInsetsCompat.Type.systemBars())?.top ?: 0
		return inset + dp(56)
	}

	/** Re-attach the popup to a (new) topmost window's decor so the card
	 * stays visible above dialogs opened while the tutorial runs (module
	 * browser, menus, guides). No-op when the wizard isn't showing. */
	fun reanchor(anchor: View) {
		val p = popup ?: return
		runCatching {
			if (p.isShowing)
				p.dismiss()
			anchor.post {
				runCatching {
					if (popup === p && anchor.isAttachedToWindow)
						p.showAtLocation(anchor, Gravity.TOP or Gravity.CENTER_HORIZONTAL, 0, topOffset())
				}
			}
		}
	}

	private fun render(direction: Int) {
		val s = steps[step]
		stepLabel.text = activity.getString(R.string.wizard_step_of, step + 1, steps.size)
		body.text = activity.getString(s.text)
		imageHolder.removeAllViews()
		val imgRow = rowOfModules(activity, s.images, 130)
		imgRow?.let {
			it.setPadding(0, dp(10), 0, 0)
			imageHolder.addView(it)
		}
		prevBtn.text = activity.getString(R.string.wizard_prev)
		prevBtn.visibility = if (step == 0) View.INVISIBLE else View.VISIBLE
		nextBtn.text = activity.getString(
			if (step == steps.size - 1) R.string.wizard_done else R.string.wizard_next)

		if (direction != 0) {
			// Body + images slide in from the direction of travel.
			stepContent.alpha = 0f
			stepContent.translationX = dp(40).toFloat() * direction
			stepContent.animate().alpha(1f).translationX(0f).setDuration(240L)
				.setInterpolator(android.view.animation.DecelerateInterpolator()).start()
			// Step counter does a quick pop.
			stepLabel.scaleX = 0.6f; stepLabel.scaleY = 0.6f; stepLabel.alpha = 0.3f
			stepLabel.animate().scaleX(1f).scaleY(1f).alpha(1f).setDuration(260L)
				.setInterpolator(android.view.animation.OvershootInterpolator()).start()
			// The card gives a tiny bounce so a step change is felt.
			cardView?.let { c ->
				c.animate().scaleX(1.015f).scaleY(1.015f).setDuration(90L)
					.withEndAction { c.animate().scaleX(1f).scaleY(1f).setDuration(150L).start() }
					.start()
			}
			// The advancing Next button pulses.
			val btn = if (direction > 0) nextBtn else prevBtn
			btn.animate().scaleX(1.18f).scaleY(1.18f).setDuration(110L)
				.withEndAction { btn.animate().scaleX(1f).scaleY(1f).setDuration(160L)
					.setInterpolator(android.view.animation.OvershootInterpolator()).start() }
				.start()
		}
		// Module images stagger in with a scale+fade, whatever the direction.
		(imgRow as? LinearLayout)?.let { row ->
			for (i in 0 until row.childCount) {
				val v = row.getChildAt(i)
				v.alpha = 0f; v.scaleX = 0.5f; v.scaleY = 0.5f
				v.animate().alpha(1f).scaleX(1f).scaleY(1f)
					.setStartDelay(60L + i * 70L).setDuration(300L)
					.setInterpolator(android.view.animation.OvershootInterpolator()).start()
			}
		}
	}

	/** Close without touching preferences (used when another tutorial
	 * replaces this one). */
	fun close() {
		popup?.dismiss()
		popup = null
	}

	private fun dismiss() {
		popup?.dismiss()
		popup = null
		// Never auto-show again once the user has seen it (basic wizard
		// only; the Pro tutorial has no auto-show to suppress).
		if (doneFlag != null)
			activity.getSharedPreferences("guide", android.content.Context.MODE_PRIVATE)
				.edit().putBoolean(doneFlag, true).apply()
	}
}


/** First-run interface tour: a full-screen scrim with a rounded spotlight cut
 * over one region of the UI at a time (toolbar, module palette, cable-park bar)
 * and a glass caption card explaining it. Focusable, so its own taps drive it
 * and the app underneath is left untouched until the tour ends. Separate from
 * the patch-building Wizard — this one teaches the interface, not a patch. */
class Tour(
	private val activity: Activity,
	private val doneFlag: String? = "tour_done",
) {
	// spot: 0 = none (centred card, no cut), 1 = toolbar, 2 = palette, 3 = cable park
	private data class Step(val title: Int, val body: Int, val spot: Int)
	private val steps = listOf(
		Step(R.string.tour_s1_t, R.string.tour_s1_b, 0),
		Step(R.string.tour_s2_t, R.string.tour_s2_b, 1),
		Step(R.string.tour_s3_t, R.string.tour_s3_b, 2),
		Step(R.string.tour_s4_t, R.string.tour_s4_b, 3),
		Step(R.string.tour_s5_t, R.string.tour_s5_b, 0),
		Step(R.string.tour_s6_t, R.string.tour_s6_b, 0),
	)

	private fun dp(v: Int) = (v * activity.resources.displayMetrics.density).toInt()
	private var step = 0
	private var popup: PopupWindow? = null
	private lateinit var scrim: ScrimView
	private lateinit var card: LinearLayout
	private lateinit var titleView: TextView
	private lateinit var bodyView: TextView
	private lateinit var stepLabel: TextView
	private lateinit var nextBtn: TextView

	/** Draws the dark scrim with a rounded hole (PorterDuff CLEAR) over the
	 * spotlighted region, plus an accent ring around it. */
	private inner class ScrimView(a: Activity) : View(a) {
		var spot: RectF? = null
		private val scrimPaint = Paint().apply { color = 0xC8000000.toInt() }
		private val clearPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
			xfermode = PorterDuffXfermode(PorterDuff.Mode.CLEAR)
		}
		private val ringPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
			style = Paint.Style.STROKE
			strokeWidth = dp(2).toFloat()
			color = AppTheme.current.accent
		}
		override fun onDraw(canvas: Canvas) {
			val r = dp(14).toFloat()
			val save = canvas.saveLayer(0f, 0f, width.toFloat(), height.toFloat(), null)
			canvas.drawRect(0f, 0f, width.toFloat(), height.toFloat(), scrimPaint)
			spot?.let { canvas.drawRoundRect(it, r, r, clearPaint) }
			canvas.restoreToCount(save)
			spot?.let { canvas.drawRoundRect(it, r, r, ringPaint) }
		}

		/** First measure, rotation, split-screen resize: the views underneath
		 * have all moved, so re-measure the card and re-aim the spotlight.
		 * Posted, because the toolbar and palette re-lay out on the same pass. */
		override fun onSizeChanged(w: Int, h: Int, oldW: Int, oldH: Int) {
			super.onSizeChanged(w, h, oldW, oldH)
			post {
				if (popup != null) {
					applyCardWidth()
					render()
				}
			}
		}
	}

	fun show() {
		if (popup != null)
			return
		val font = AppFont.get(activity)
		fun tv(size: Float, color: Int, bold: Boolean = false) = TextView(activity).apply {
			textSize = size; setTextColor(color)
			setTypeface(font, if (bold) Typeface.BOLD else Typeface.NORMAL)
		}

		scrim = ScrimView(activity).apply {
			isClickable = true
			setOnClickListener { advance() } // tapping the dimmed area advances
		}
		val root = FrameLayout(activity)
		root.addView(scrim, FrameLayout.LayoutParams(
			ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT))

		card = LinearLayout(activity).apply {
			orientation = LinearLayout.VERTICAL
			isClickable = true // swallow taps on the card so they don't advance
			background = GradientDrawable().apply {
				cornerRadius = dp(20).toFloat()
				setColor(AppTheme.withAlpha(AppTheme.current.surface, 96))
				setStroke(dp(1), AppTheme.withAlpha(Color.WHITE, 20))
			}
			setPadding(dp(20), dp(15), dp(20), dp(11))
		}
		val header = LinearLayout(activity).apply {
			orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL
		}
		titleView = tv(16f, AppTheme.current.accent, bold = true).apply {
			layoutParams = LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f)
		}
		stepLabel = tv(12f, AppTheme.current.textSecondary)
		header.addView(titleView); header.addView(stepLabel)
		card.addView(header)
		bodyView = tv(14.5f, AppTheme.current.textPrimary).apply { setPadding(0, dp(9), 0, dp(12)) }
		card.addView(bodyView)
		val buttons = LinearLayout(activity).apply {
			orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL
		}
		val skip = tv(13.5f, AppTheme.current.textSecondary).apply {
			text = activity.getString(R.string.tour_skip)
			setPadding(dp(4), dp(8), dp(16), dp(8))
			setOnClickListener { finish() }
		}
		val spacer = View(activity).apply {
			layoutParams = LinearLayout.LayoutParams(0, 1, 1f)
		}
		nextBtn = tv(15f, AppTheme.current.accent, bold = true).apply {
			setPadding(dp(16), dp(8), dp(4), dp(8))
			setOnClickListener { advance() }
		}
		buttons.addView(skip); buttons.addView(spacer); buttons.addView(nextBtn)
		card.addView(buttons)

		// Starting width only: applyCardWidth() re-fits it to the scrim as soon
		// as that is measured, and again after every rotation or resize.
		val cardW = (activity.resources.displayMetrics.widthPixels * 0.9f).toInt()
			.coerceAtMost(dp(560))
		root.addView(card, FrameLayout.LayoutParams(
			cardW, ViewGroup.LayoutParams.WRAP_CONTENT))

		val p = PopupWindow(root,
			ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT)
		p.isFocusable = true
		popup = p
		render()
		activity.window.decorView.post {
			runCatching {
				p.showAtLocation(activity.window.decorView, Gravity.NO_GRAVITY, 0, 0)
				root.alpha = 0f
				root.animate().alpha(1f).setDuration(240L).start()
			}
		}
	}

	private fun advance() {
		if (step < steps.size - 1) { step++; render() } else finish()
	}

	private fun render() {
		val s = steps[step]
		titleView.text = activity.getString(s.title)
		bodyView.text = activity.getString(s.body)
		stepLabel.text = activity.getString(R.string.wizard_step_of, step + 1, steps.size)
		nextBtn.text = activity.getString(
			if (step == steps.size - 1) R.string.wizard_done else R.string.wizard_next)

		val rect = spotRect(s.spot)
		scrim.spot = rect
		scrim.invalidate()
		positionCard(rect)

		card.alpha = 0f; card.translationY = dp(14).toFloat()
		card.animate().alpha(1f).translationY(0f).setDuration(240L)
			.setInterpolator(android.view.animation.DecelerateInterpolator()).start()
	}

	/** Fits the card to the scrim: nine tenths of the width, capped so it does
	 * not stretch into an unreadable line on a tablet or a desktop window. */
	private fun applyCardWidth() {
		val w = scrim.width
		if (w <= 0) return
		val lp = card.layoutParams as FrameLayout.LayoutParams
		lp.width = (w * 0.9f).toInt().coerceAtMost(dp(560))
		card.layoutParams = lp
	}

	/** Puts the card clear of the spotlight: beside a narrow vertical bar, or in
	 * the free band above/below a full-width one. Every distance is a fraction
	 * of the scrim or a gap in dp, so this holds in portrait, in landscape and
	 * at any screen size -- the fixed pixel margins it replaces did not. */
	private fun positionCard(spot: RectF?) {
		val lp = card.layoutParams as FrameLayout.LayoutParams
		val w = scrim.width.toFloat(); val h = scrim.height.toFloat()
		lp.leftMargin = 0; lp.rightMargin = 0; lp.topMargin = 0; lp.bottomMargin = 0
		val gap = dp(16)
		when {
			spot == null || w <= 0f || h <= 0f -> lp.gravity = Gravity.CENTER
			// Narrow and tall (the cable parking bar): sit next to it, on the
			// opposite side, rather than trying to squeeze above or below.
			spot.width() < w * 0.4f -> {
				val onLeft = spot.centerX() < w * 0.5f
				lp.gravity = Gravity.CENTER_VERTICAL or
					(if (onLeft) Gravity.END else Gravity.START)
				if (onLeft) lp.rightMargin = gap else lp.leftMargin = gap
			}
			// A full-width band near the top: the card goes underneath it.
			spot.centerY() < h * 0.5f -> {
				lp.gravity = Gravity.TOP or Gravity.CENTER_HORIZONTAL
				lp.topMargin = (spot.bottom + gap).toInt().coerceAtMost((h * 0.7f).toInt())
			}
			// A full-width band near the bottom: the card goes above it.
			else -> {
				lp.gravity = Gravity.BOTTOM or Gravity.CENTER_HORIZONTAL
				lp.bottomMargin = (h - spot.top + gap).toInt().coerceAtMost((h * 0.7f).toInt())
			}
		}
		card.layoutParams = lp
	}

	/** Screen-space rect → scrim-space, and clipped to the scrim. The tour is a
	 * PopupWindow: its origin is only the screen origin when the app owns the
	 * whole display, which is not true in a freeform/desktop window or with
	 * insets on one side. Everything below measures the real thing on screen. */
	private fun toScrimSpace(r: android.graphics.Rect): RectF? {
		val loc = IntArray(2)
		scrim.getLocationOnScreen(loc)
		val out = RectF(
			(r.left - loc[0]).toFloat(), (r.top - loc[1]).toFloat(),
			(r.right - loc[0]).toFloat(), (r.bottom - loc[1]).toFloat())
		val w = scrim.width.toFloat(); val h = scrim.height.toFloat()
		if (w <= 0f || h <= 0f) return null
		// Entirely outside (a bar that is switched off, say): no spotlight.
		if (out.right <= 0f || out.bottom <= 0f || out.left >= w || out.top >= h)
			return null
		out.intersect(0f, 0f, w, h)
		return if (out.width() < 1f || out.height() < 1f) null else out
	}

	/** The region to spotlight, measured from the live views. Falls back to a
	 * band proportional to the scrim -- never to fixed pixel sizes, which is
	 * what used to leave the ring next to the palette instead of around it. */
	private fun spotRect(spot: Int): RectF? {
		val w = scrim.width.toFloat(); val h = scrim.height.toFloat()
		if (w <= 0f || h <= 0f) return null
		val main = activity as? MainActivity
		val pad = dp(2).toFloat()
		return when (spot) {
			1 -> main?.toolbarBounds()?.let { toScrimSpace(it) }?.apply { inset(-pad, -pad) }
				?: RectF(dp(6).toFloat(), dp(4).toFloat(), w - dp(6), h * 0.22f)
			2 -> main?.paletteBounds()?.let { toScrimSpace(it) }?.apply { inset(-pad, -pad) }
				?: RectF(dp(5).toFloat(), h * 0.88f, w - dp(5), h - dp(4))
			3 -> main?.cableParkBounds()?.let { toScrimSpace(it) }?.apply { inset(-pad, -pad) }
				?: RectF(0f, h * 0.30f, w * 0.12f, h * 0.70f)
			else -> null
		}
	}

	private fun finish() {
		popup?.let { runCatching { it.dismiss() } }
		popup = null
		if (doneFlag != null)
			activity.getSharedPreferences("guide", Context.MODE_PRIVATE)
				.edit().putBoolean(doneFlag, true).apply()
	}
}
