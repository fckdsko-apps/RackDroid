package org.rackdroid

import android.app.Activity
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.graphics.Typeface
import android.os.Bundle
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

/**
 * Experimental A/B controller for RackDroid's Android audio backend.
 *
 * Deliberately separate from MainActivity so the production rack UI and its
 * toolbar/menu code do not need to change for a temporary measurement build.
 * This Activity writes a tiny startup config and reads diagnostics. It never
 * mutates a live Oboe stream or Rack engine mode.
 */
class AudioLabActivity : Activity() {

	private data class Options(
		val oboeManagedCallback: Boolean = false,
		val fastEngine: Boolean = false,
		val inputEnabled: Boolean = true,
	)

	private lateinit var callbackBox: CheckBox
	private lateinit var fastEngineBox: CheckBox
	private lateinit var inputBox: CheckBox
	private lateinit var statusView: TextView

	private val configFile: File
		get() = File(filesDir, "user/audio-lab.cfg")

	override fun onCreate(savedInstanceState: Bundle?) {
		super.onCreate(savedInstanceState)
		title = "RackDroid Audio Lab"

		val root = LinearLayout(this).apply {
			orientation = LinearLayout.VERTICAL
			setPadding(dp(18), dp(18), dp(18), dp(24))
		}

		root.addView(TextView(this).apply {
			text = "RackDroid Audio Lab"
			textSize = 24f
			setTypeface(typeface, Typeface.BOLD)
		})
		root.addView(TextView(this).apply {
			text = "These are startup-only A/B switches. After Save + restart, load the same test patch, open Audio Lab, tap Reset measurement + back to Rack, run 30–60 seconds, then reopen Audio Lab and copy the diagnostics."
			textSize = 14f
			setPadding(0, dp(8), 0, dp(14))
		})

		val opts = readOptions()
		callbackBox = CheckBox(this).apply {
			text = "Let Oboe choose callback size\nOff = current RackDroid fixed Rack block callback"
			isChecked = opts.oboeManagedCallback
		}
		fastEngineBox = CheckBox(this).apply {
			text = "Fast single-thread Rack engine\nOnly active when Rack Engine threads = 1; Off = upstream Rack dispatch"
			isChecked = opts.fastEngine
		}
		inputBox = CheckBox(this).apply {
			text = "Enable audio input stream\nOff = output-only; do not use when the patch needs live audio input"
			isChecked = opts.inputEnabled
		}
		root.addView(callbackBox)
		root.addView(fastEngineBox)
		root.addView(inputBox)

		root.addView(TextView(this).apply {
			text = "LIVE DIAGNOSTICS"
			textSize = 13f
			setTypeface(typeface, Typeface.BOLD)
			setPadding(0, dp(18), 0, dp(6))
		})
		statusView = TextView(this).apply {
			typeface = Typeface.MONOSPACE
			textSize = 11f
			setTextIsSelectable(true)
			setPadding(dp(10), dp(10), dp(10), dp(10))
		}
		root.addView(statusView, LinearLayout.LayoutParams(
			ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT))

		val buttons = LinearLayout(this).apply {
			orientation = LinearLayout.VERTICAL
			gravity = Gravity.CENTER_HORIZONTAL
			setPadding(0, dp(12), 0, 0)
		}
		buttons.addView(Button(this).apply {
			text = "Reset measurement + back to Rack"
			setOnClickListener {
				val ok = runCatching { nativeResetAudioMeasurements() }.getOrDefault(false)
				if (ok) {
					Toast.makeText(this@AudioLabActivity, "Measurement window reset", Toast.LENGTH_SHORT).show()
					returnToRack()
				} else {
					Toast.makeText(this@AudioLabActivity, "Audio stream is not active", Toast.LENGTH_SHORT).show()
					refreshStatus()
				}
			}
		}, fullWidthParams())
		buttons.addView(Button(this).apply {
			text = "Refresh diagnostics"
			setOnClickListener { refreshStatus() }
		}, fullWidthParams())
		buttons.addView(Button(this).apply {
			text = "Copy diagnostics"
			setOnClickListener {
				val cm = getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
				cm.setPrimaryClip(ClipData.newPlainText("RackDroid audio diagnostics", statusView.text))
				Toast.makeText(this@AudioLabActivity, "Diagnostics copied", Toast.LENGTH_SHORT).show()
			}
		}, fullWidthParams())
		buttons.addView(Button(this).apply {
			text = "Save + restart RackDroid"
			setOnClickListener {
				if (writeOptions(currentOptions())) restartRackDroid()
			}
		}, fullWidthParams())
		buttons.addView(Button(this).apply {
			text = "Restore baseline + restart"
			setOnClickListener {
				callbackBox.isChecked = false
				fastEngineBox.isChecked = false
				inputBox.isChecked = true
				if (writeOptions(currentOptions())) restartRackDroid()
			}
		}, fullWidthParams())
		buttons.addView(Button(this).apply {
			text = "Back to Rack"
			setOnClickListener { returnToRack() }
		}, fullWidthParams())
		root.addView(buttons)

		setContentView(ScrollView(this).apply { addView(root) })
	}

	override fun onResume() {
		super.onResume()
		if (::statusView.isInitialized)
			refreshStatus()
	}

	@Suppress("DEPRECATION")
	override fun onBackPressed() {
		returnToRack()
	}

	private fun fullWidthParams() = LinearLayout.LayoutParams(
		ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT
	).apply { setMargins(0, dp(3), 0, dp(3)) }

	private fun dp(v: Int): Int = (v * resources.displayMetrics.density).toInt()

	private fun refreshStatus() {
		statusView.text = runCatching { nativeGetAudioStatus() }
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
			append("# RackDroid v06 Audio Lab; read once at native audio startup.\n")
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
			Toast.makeText(this, "Could not save Audio Lab settings", Toast.LENGTH_LONG).show()
		return ok
	}

	private fun returnToRack() {
		// Bring the existing NativeActivity back if it is underneath us; if Audio
		// Lab was launched while RackDroid was not running, start it normally.
		startActivity(Intent(this, MainActivity::class.java).apply {
			addFlags(Intent.FLAG_ACTIVITY_REORDER_TO_FRONT)
		})
		finish()
	}

	private fun restartRackDroid() {
		// Explicit MainActivity avoids changing normal package-launch resolution.
		// This mirrors RackDroid's existing full-process restart pattern: enqueue
		// the fresh Activity launch, then end this process so native state, loaded
		// plugins and the audio stream are rebuilt from the new startup config.
		val intent = Intent(this, MainActivity::class.java).apply {
			addFlags(Intent.FLAG_ACTIVITY_CLEAR_TASK or Intent.FLAG_ACTIVITY_NEW_TASK)
		}
		startActivity(intent)
		Runtime.getRuntime().exit(0)
	}

	private external fun nativeGetAudioStatus(): String
	private external fun nativeResetAudioMeasurements(): Boolean

	companion object {
		init {
			// Same pair MainActivity explicitly loads. rack_engine provides Rack;
			// rackdroid provides the Oboe backend and this Activity's JNI status.
			System.loadLibrary("rack_engine")
			System.loadLibrary("rackdroid")
		}
	}
}
