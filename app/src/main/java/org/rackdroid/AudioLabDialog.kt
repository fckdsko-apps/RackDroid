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
import android.widget.RadioButton
import android.widget.RadioGroup
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import java.io.File
import java.nio.file.Files
import java.nio.file.StandardCopyOption

/** Audio Lab UI hosted by MainActivity, so opening it does not replace the rack. */
class AudioLabDialog(private val activity: MainActivity) {

	private data class Options(
		// -1 = Oboe-managed, 0 = Rack block, positive = explicit Android frames.
		val callbackFrames: Int = 0,
		// 0 = v07 Rack-requested + Medium SRC, -1 = native unspecified, 44100 = explicit.
		val sampleRateMode: Int = 0,
		// 0 = system/default, 1 or 2 = hardware-burst multiples.
		val bufferBursts: Int = 0,
		val fastEngine: Boolean = false,
		val inputEnabled: Boolean = true,
	)

	private lateinit var callbackGroup: RadioGroup
	private lateinit var callbackRack: RadioButton
	private lateinit var callback192: RadioButton
	private lateinit var callback128: RadioButton
	private lateinit var callback96: RadioButton
	private lateinit var callbackOboe: RadioButton

	private lateinit var sampleRateGroup: RadioGroup
	private lateinit var sampleRateControl: RadioButton
	private lateinit var sampleRateNative: RadioButton
	private lateinit var sampleRate44100: RadioButton

	private lateinit var bufferGroup: RadioGroup
	private lateinit var bufferDefault: RadioButton
	private lateinit var buffer2Bursts: RadioButton
	private lateinit var buffer1Burst: RadioButton

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
			text = "v08.1 low-latency-path lab. Keep callback, engine and input fixed while testing sample-rate and output-buffer modes one variable at a time. Save + restart after changing a startup setting."
			textSize = 14f
			setPadding(0, dp(8), 0, dp(14))
		})

		val opts = readOptions()

		addSectionLabel(root, "ANDROID OUTPUT CALLBACK")
		callbackGroup = RadioGroup(activity).apply { orientation = RadioGroup.VERTICAL }
		fun callbackChoice(label: String, frames: Int) = RadioButton(activity).apply {
			text = label
			tag = frames
		}
		callbackRack = callbackChoice("Rack block (v07 winner/control; 256 in current patch)", 0)
		callback192 = callbackChoice("Fixed 192 frames (Android-only)", 192)
		callback128 = callbackChoice("Fixed 128 frames (Android-only)", 128)
		callback96 = callbackChoice("Fixed 96 frames (Android-only)", 96)
		callbackOboe = callbackChoice("Oboe-managed / unspecified", -1)
		listOf(callbackRack, callback192, callback128, callback96, callbackOboe).forEach { callbackGroup.addView(it) }
		when (opts.callbackFrames) {
			192 -> callback192.isChecked = true
			128 -> callback128.isChecked = true
			96 -> callback96.isChecked = true
			-1 -> callbackOboe.isChecked = true
			else -> callbackRack.isChecked = true
		}
		root.addView(callbackGroup)

		addSectionLabel(root, "OUTPUT SAMPLE-RATE PATH")
		sampleRateGroup = RadioGroup(activity).apply { orientation = RadioGroup.VERTICAL }
		fun sampleRateChoice(label: String, mode: Int) = RadioButton(activity).apply {
			text = label
			tag = mode
		}
		sampleRateControl = sampleRateChoice(
			"Pinned 48 kHz output + Oboe Medium SRC (v07 control)", 0)
		sampleRateNative = sampleRateChoice(
			"Native / unspecified output rate + Oboe SRC off", -1)
		sampleRate44100 = sampleRateChoice(
			"Explicit 44.1 kHz output + Oboe SRC off", 44100)
		listOf(sampleRateControl, sampleRateNative, sampleRate44100).forEach { sampleRateGroup.addView(it) }
		when (opts.sampleRateMode) {
			-1 -> sampleRateNative.isChecked = true
			44100 -> sampleRate44100.isChecked = true
			else -> sampleRateControl.isChecked = true
		}
		root.addView(sampleRateGroup)

		root.addView(TextView(activity).apply {
			text = "Rack autosaves the active patch, so v08.1 deliberately ignores .vcv/autosave sample-rate requests when choosing the Android output stream. The control is pinned to 48 kHz; Native/44.1 modes override it. Rack still receives the actual opened stream rate for correct engine/device math. It is fine if this disposable test patch autosaves."
			textSize = 12f
			setPadding(dp(6), 0, 0, dp(6))
		})

		addSectionLabel(root, "AAUDIO OUTPUT BUFFER")
		bufferGroup = RadioGroup(activity).apply { orientation = RadioGroup.VERTICAL }
		fun bufferChoice(label: String, bursts: Int) = RadioButton(activity).apply {
			text = label
			tag = bursts
		}
		bufferDefault = bufferChoice("System/default size (v07 control)", 0)
		buffer2Bursts = bufferChoice("Request 2 × reported hardware burst", 2)
		buffer1Burst = bufferChoice("Request 1 × reported hardware burst", 1)
		listOf(bufferDefault, buffer2Bursts, buffer1Burst).forEach { bufferGroup.addView(it) }
		when (opts.bufferBursts) {
			1 -> buffer1Burst.isChecked = true
			2 -> buffer2Bursts.isChecked = true
			else -> bufferDefault.isChecked = true
		}
		root.addView(bufferGroup)

		root.addView(TextView(activity).apply {
			text = "AAudio may clamp a buffer request. The diagnostics show the requested and granted frame counts; judge smaller buffers by latency and xruns, not by the request alone."
			textSize = 12f
			setPadding(dp(6), 0, 0, dp(8))
		})

		fastEngineBox = CheckBox(activity).apply {
			text = "Fast single-thread Rack engine\nOnly active when Rack Engine threads = 1; Off = upstream Rack dispatch"
			isChecked = opts.fastEngine
		}
		inputBox = CheckBox(activity).apply {
			text = "Enable audio input stream\nOff = output-only; do not use when the patch needs live audio input"
			isChecked = opts.inputEnabled
		}
		root.addView(fastEngineBox)
		root.addView(inputBox)

		addSectionLabel(root, "LIVE DIAGNOSTICS", topPadding = 18)
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
			text = "Restore v08 control + restart"
			setOnClickListener {
				callbackRack.isChecked = true
				sampleRateControl.isChecked = true
				bufferDefault.isChecked = true
				fastEngineBox.isChecked = true
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

	private fun addSectionLabel(root: LinearLayout, label: String, topPadding: Int = 10) {
		root.addView(TextView(activity).apply {
			text = label
			textSize = 13f
			setTypeface(typeface, Typeface.BOLD)
			setPadding(0, dp(topPadding), 0, dp(6))
		})
	}

	private fun fullWidthParams() = LinearLayout.LayoutParams(
		ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT
	).apply { setMargins(0, dp(3), 0, dp(3)) }

	private fun dp(v: Int): Int = (v * activity.resources.displayMetrics.density).toInt()

	private fun refreshStatus() {
		statusView.text = runCatching { AudioLabActivity.nativeGetAudioStatus() }
			.getOrElse { "Native diagnostics unavailable: ${it.javaClass.simpleName}: ${it.message}" }
	}

	private fun checkedTag(group: RadioGroup, fallback: Int): Int {
		val checked = group.findViewById<RadioButton>(group.checkedRadioButtonId)
		return (checked?.tag as? Int) ?: fallback
	}

	private fun currentOptions() = Options(
		callbackFrames = checkedTag(callbackGroup, 0),
		sampleRateMode = checkedTag(sampleRateGroup, 0),
		bufferBursts = checkedTag(bufferGroup, 0),
		fastEngine = fastEngineBox.isChecked,
		inputEnabled = inputBox.isChecked,
	)

	private fun readOptions(): Options {
		val file = configFile
		if (!file.isFile) return Options()
		var callbackFrames = 0
		var callbackFramesSeen = false
		var sampleRateMode = 0
		var bufferBursts = 0
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

				when (key) {
					"callback_frames" -> {
						val frames = value.toIntOrNull()
						if (frames != null && frames in setOf(-1, 0, 96, 128, 192, 256)) {
							callbackFrames = frames
							callbackFramesSeen = true
						}
						return@forEachLine
					}
					"sample_rate_mode" -> {
						val mode = value.toIntOrNull()
						if (mode != null && mode in setOf(-1, 0, 44100))
							sampleRateMode = mode
						return@forEachLine
					}
					"buffer_bursts" -> {
						val bursts = value.toIntOrNull()
						if (bursts != null && bursts in setOf(0, 1, 2))
							bufferBursts = bursts
						return@forEachLine
					}
				}

				val parsed = when (value.lowercase()) {
					"1", "true", "on", "yes" -> true
					"0", "false", "off", "no" -> false
					else -> null
				} ?: return@forEachLine
				when (key) {
					"native_callback" -> if (!callbackFramesSeen) callbackFrames = if (parsed) -1 else 0
					"fast_engine" -> fastEngine = parsed
					"input_enabled" -> inputEnabled = parsed
				}
			}
		}
		return Options(callbackFrames, sampleRateMode, bufferBursts, fastEngine, inputEnabled)
	}

	private fun writeOptions(options: Options): Boolean {
		val dir = configFile.parentFile ?: return false
		dir.mkdirs()
		val tmp = File(dir, "audio-lab.cfg.tmp")
		val text = buildString {
			append("# RackDroid v08.1 Low-Latency-Path Lab; read once at native audio startup.\n")
			// Keep the v06 key so a downgrade can still interpret Oboe-managed mode.
			append("native_callback=").append(if (options.callbackFrames < 0) 1 else 0).append('\n')
			append("callback_frames=").append(options.callbackFrames).append('\n')
			append("sample_rate_mode=").append(options.sampleRateMode).append('\n')
			append("buffer_bursts=").append(options.bufferBursts).append('\n')
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
