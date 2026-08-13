package org.rackdroid

import android.app.AlertDialog
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.graphics.Typeface
import android.view.Gravity
import android.view.ViewGroup
import android.widget.Button
import android.widget.CheckBox
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import java.io.File
import java.nio.file.Files
import java.nio.file.StandardCopyOption

/** Audio Lab UI hosted by MainActivity, so opening it does not replace the rack. */
class AudioLabDialog(private val activity: MainActivity) {

	private data class Options(
		val oboeManagedCallback: Boolean = false,
		val fastEngine: Boolean = false,
		val inputEnabled: Boolean = true,
	)

	private lateinit var callbackBox: CheckBox
	private lateinit var fastEngineBox: CheckBox
	private lateinit var inputBox: CheckBox
	private lateinit var statusView: TextView
	private var dialog: AlertDialog? = null

	private val configFile: File
		get() = File(activity.filesDir, "user/audio-lab.cfg")

	fun show() {
		val root = LinearLayout(activity).apply {
			orientation = LinearLayout.VERTICAL
			setPadding(dp(18), dp(18), dp(18), dp(24))
		}

		root.addView(TextView(activity).apply {
			text = "RackDroid Audio Lab"
			textSize = 22f
			setTypeface(typeface, Typeface.BOLD)
		})
		root.addView(TextView(activity).apply {
			text = "Startup-only A/B switches. Save + restart after changing them. For a measurement, load the same patch, open Audio Lab, tap Reset measurement + close, wait 60 seconds, then reopen Audio Lab and copy diagnostics."
			textSize = 14f
			setPadding(0, dp(8), 0, dp(14))
		})

		val opts = readOptions()
		callbackBox = CheckBox(activity).apply {
			text = "Let Oboe choose callback size\nOff = current RackDroid fixed Rack block callback"
			isChecked = opts.oboeManagedCallback
		}
		fastEngineBox = CheckBox(activity).apply {
			text = "Fast single-thread Rack engine\nOnly active when Rack Engine threads = 1; Off = upstream Rack dispatch"
			isChecked = opts.fastEngine
		}
		inputBox = CheckBox(activity).apply {
			text = "Enable audio input stream\nOff = output-only; do not use when the patch needs live audio input"
			isChecked = opts.inputEnabled
		}
		root.addView(callbackBox)
		root.addView(fastEngineBox)
		root.addView(inputBox)

		root.addView(TextView(activity).apply {
			text = "LIVE DIAGNOSTICS"
			textSize = 13f
			setTypeface(typeface, Typeface.BOLD)
			setPadding(0, dp(18), 0, dp(6))
		})
		statusView = TextView(activity).apply {
			typeface = Typeface.MONOSPACE
			textSize = 11f
			setTextIsSelectable(true)
			setPadding(dp(10), dp(10), dp(10), dp(10))
		}
		root.addView(statusView, LinearLayout.LayoutParams(
			ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT))

		val buttons = LinearLayout(activity).apply {
			orientation = LinearLayout.VERTICAL
			gravity = Gravity.CENTER_HORIZONTAL
			setPadding(0, dp(12), 0, 0)
		}
		buttons.addView(Button(activity).apply {
			text = "Reset measurement + close"
			setOnClickListener {
				// Close first, then begin the measurement after the dialog teardown
				// so the test window does not include Audio Lab's own dismissal work.
				dialog?.dismiss()
				activity.window.decorView.postDelayed({
					val ok = runCatching { AudioLabActivity.nativeResetAudioMeasurements() }
						.getOrDefault(false)
					if (!ok)
						Toast.makeText(activity, "Audio stream is not active", Toast.LENGTH_LONG).show()
				}, 150L)
			}
		}, fullWidthParams())
		buttons.addView(Button(activity).apply {
			text = "Refresh diagnostics"
			setOnClickListener { refreshStatus() }
		}, fullWidthParams())
		buttons.addView(Button(activity).apply {
			text = "Copy diagnostics"
			setOnClickListener {
				val cm = activity.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
				cm.setPrimaryClip(ClipData.newPlainText("RackDroid audio diagnostics", statusView.text))
				Toast.makeText(activity, "Diagnostics copied", Toast.LENGTH_SHORT).show()
			}
		}, fullWidthParams())
		buttons.addView(Button(activity).apply {
			text = "Save + restart RackDroid"
			setOnClickListener {
				if (writeOptions(currentOptions())) restartRackDroid()
			}
		}, fullWidthParams())
		buttons.addView(Button(activity).apply {
			text = "Restore baseline + restart"
			setOnClickListener {
				callbackBox.isChecked = false
				fastEngineBox.isChecked = false
				inputBox.isChecked = true
				if (writeOptions(currentOptions())) restartRackDroid()
			}
		}, fullWidthParams())

		root.addView(buttons)
		val scroll = ScrollView(activity).apply { addView(root) }
		dialog = AlertDialog.Builder(activity)
			.setView(scroll)
			.setNegativeButton(android.R.string.cancel, null)
			.create()
		dialog?.setOnShowListener { refreshStatus() }
		dialog?.show()
	}

	private fun fullWidthParams() = LinearLayout.LayoutParams(
		ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT
	).apply { setMargins(0, dp(3), 0, dp(3)) }

	private fun dp(v: Int): Int = (v * activity.resources.displayMetrics.density).toInt()

	private fun refreshStatus() {
		statusView.text = runCatching { AudioLabActivity.nativeGetAudioStatus() }
			.getOrElse { "Native diagnostics unavailable: ${it.javaClass.simpleName}: ${it.message}" }
	}

	private fun currentOptions() = Options(
		callbackBox.isChecked,
		fastEngineBox.isChecked,
		inputBox.isChecked,
	)

	private fun readOptions(): Options {
		val file = configFile
		if (!file.isFile) return Options()
		var callback = false
		var fastEngine = false
		var inputEnabled = true
		runCatching {
			file.forEachLine { raw ->
				val line = raw.trim()
				if (line.isEmpty() || line.startsWith("#")) return@forEachLine
				val eq = line.indexOf('=')
				if (eq <= 0) return@forEachLine
				val key = line.substring(0, eq).trim()
				val value = line.substring(eq + 1).trim()
				val parsed = when (value.lowercase()) {
					"1", "true", "on", "yes" -> true
					"0", "false", "off", "no" -> false
					else -> null
				} ?: return@forEachLine
				when (key) {
					"native_callback" -> callback = parsed
					"fast_engine" -> fastEngine = parsed
					"input_enabled" -> inputEnabled = parsed
				}
			}
		}
		return Options(callback, fastEngine, inputEnabled)
	}

	private fun writeOptions(options: Options): Boolean {
		val dir = configFile.parentFile ?: return false
		dir.mkdirs()
		val tmp = File(dir, "audio-lab.cfg.tmp")
		val text = buildString {
			append("# RackDroid v06.1 Audio Lab; read once at native audio startup.\n")
			append("native_callback=").append(if (options.oboeManagedCallback) 1 else 0).append('\n')
			append("fast_engine=").append(if (options.fastEngine) 1 else 0).append('\n')
			append("input_enabled=").append(if (options.inputEnabled) 1 else 0).append('\n')
		}
		val ok = runCatching {
			tmp.writeText(text)
			try {
				Files.move(tmp.toPath(), configFile.toPath(),
					StandardCopyOption.REPLACE_EXISTING, StandardCopyOption.ATOMIC_MOVE)
			} catch (_: Throwable) {
				Files.move(tmp.toPath(), configFile.toPath(), StandardCopyOption.REPLACE_EXISTING)
			}
			true
		}.getOrElse {
			runCatching { tmp.delete() }
			false
		}
		if (!ok)
			Toast.makeText(activity, "Could not save Audio Lab settings", Toast.LENGTH_LONG).show()
		return ok
	}

	private fun restartRackDroid() {
		val intent = Intent(activity, MainActivity::class.java).apply {
			addFlags(Intent.FLAG_ACTIVITY_CLEAR_TASK or Intent.FLAG_ACTIVITY_NEW_TASK)
		}
		activity.startActivity(intent)
		Runtime.getRuntime().exit(0)
	}
}
