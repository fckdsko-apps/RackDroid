package org.rackdroid

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.RectF
import android.util.SparseIntArray
import android.view.MotionEvent
import android.view.View
import androidx.core.util.forEach
import androidx.core.util.isEmpty

/** Multi-touch on-screen piano: presses/releases play through Rack's own
 * "Computer keyboard/mouse" MIDI driver (native/port/keyboard_native.cpp ->
 * rack::keyboard::press/release, third_party/Rack/src/keyboard.cpp) using
 * the exact same QWERTY-row key codes it maps to notes -- so it plays
 * through whatever MIDI-CV module is already set to "Computer
 * keyboard/mouse" (the tutorial patch's default) with no new MIDI
 * plumbing, and octave state is tracked by that driver already.
 *
 * NOTE_KEYS[i] is the GLFW key code (== ASCII code for every key on this
 * row, so no GLFW header needed here) for scale degree i within the
 * driver's current octave; index 0 is the driver's "C". 32 entries span
 * every key third_party/Rack/src/keyboard.cpp's QWERTY device maps,
 * combining its two overlapping rows into one non-overlapping range for a
 * playable ~2.5 octaves without needing octave shifts for most patches. */
private val NOTE_KEYS = intArrayOf(
	'Z'.code, 'S'.code, 'X'.code, 'D'.code, 'C'.code, 'V'.code, 'G'.code, 'B'.code,
	'H'.code, 'N'.code, 'J'.code, 'M'.code, ','.code, 'L'.code, '.'.code, ';'.code,
	'/'.code, 'R'.code, '5'.code, 'T'.code, '6'.code, 'Y'.code, '7'.code, 'U'.code,
	'I'.code, '9'.code, 'O'.code, '0'.code, 'P'.code, '['.code, '='.code, ']'.code,
)
private val BLACK_SEMITONES = setOf(1, 3, 6, 8, 10)
private fun isBlack(noteOffset: Int) = (noteOffset % 12) in BLACK_SEMITONES

class PianoKeyboardView(context: Context) : View(context) {
	var onPress: (Int) -> Unit = {}
	var onRelease: (Int) -> Unit = {}

	private val whiteOffsets = NOTE_KEYS.indices.filter { !isBlack(it) }
	private val blackOffsets = NOTE_KEYS.indices.filter { isBlack(it) }
	private val whiteRects = HashMap<Int, RectF>() // noteOffset -> key rect
	private val blackRects = HashMap<Int, RectF>()

	// Which key (noteOffset) each active touch pointer currently holds, so
	// dragging across keys or lifting a finger releases the right note.
	private val pointerNotes = SparseIntArray()

	// Keys themselves stay neutral (a piano doesn't change skin with the app
	// theme); only the press-highlight follows the current accent color.
	private val whitePaint = Paint().apply { color = Color.parseColor("#ECEFF3") }
	private var whitePressedPaint = Paint().apply { color = AppTheme.current.accent }
	private val blackPaint = Paint().apply { color = Color.parseColor("#1A1D24") }
	private var blackPressedPaint = Paint().apply { color = AppTheme.current.accent }
	private val borderPaint = Paint().apply {
		color = Color.parseColor("#5A606B")
		style = Paint.Style.STROKE
		strokeWidth = 2f
	}

	/** Re-reads the accent color after a theme change. */
	fun applyTheme() {
		whitePressedPaint = Paint().apply { color = AppTheme.current.accent }
		blackPressedPaint = Paint().apply { color = AppTheme.current.accent }
		invalidate()
	}

	override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
		super.onSizeChanged(w, h, oldw, oldh)
		if (w <= 0 || h <= 0 || whiteOffsets.isEmpty()) return
		val keyWidth = w.toFloat() / whiteOffsets.size
		whiteRects.clear()
		blackRects.clear()
		for ((i, offset) in whiteOffsets.withIndex()) {
			whiteRects[offset] = RectF(i * keyWidth, 0f, (i + 1) * keyWidth, h.toFloat())
		}
		val blackWidth = keyWidth * 0.62f
		val blackHeight = h * 0.62f
		for (offset in blackOffsets) {
			val precedingWhiteIndex = whiteOffsets.count { it < offset } - 1
			val centerX = (precedingWhiteIndex + 1) * keyWidth
			blackRects[offset] = RectF(centerX - blackWidth / 2, 0f, centerX + blackWidth / 2, blackHeight)
		}
	}

	override fun onDraw(canvas: Canvas) {
		val held = HashSet<Int>()
		pointerNotes.forEach { _, note -> held.add(note) }
		for ((offset, rect) in whiteRects) {
			canvas.drawRect(rect, if (offset in held) whitePressedPaint else whitePaint)
			canvas.drawRect(rect, borderPaint)
		}
		for ((offset, rect) in blackRects) {
			canvas.drawRect(rect, if (offset in held) blackPressedPaint else blackPaint)
		}
	}

	private fun keyAt(x: Float, y: Float): Int? {
		// Black keys are drawn on top and are shorter: check them first.
		for ((offset, rect) in blackRects) {
			if (rect.contains(x, y)) return offset
		}
		for ((offset, rect) in whiteRects) {
			if (rect.contains(x, y)) return offset
		}
		return null
	}

	private fun pressPointer(pointerId: Int, x: Float, y: Float) {
		val note = keyAt(x, y) ?: return
		pointerNotes.put(pointerId, note)
		onPress(NOTE_KEYS[note])
		invalidate()
	}

	private fun movePointer(pointerId: Int, x: Float, y: Float) {
		val current = pointerNotes.get(pointerId, -1)
		val note = keyAt(x, y)
		if (note == null || note == current) return
		if (current >= 0) onRelease(NOTE_KEYS[current])
		pointerNotes.put(pointerId, note)
		onPress(NOTE_KEYS[note])
		invalidate()
	}

	private fun releasePointer(pointerId: Int) {
		val note = pointerNotes.get(pointerId, -1)
		if (note < 0) return
		pointerNotes.delete(pointerId)
		onRelease(NOTE_KEYS[note])
		invalidate()
	}

	/** Releases every held note, e.g. when the keyboard is hidden or the
	 * dialog holding it goes away -- otherwise a note could sustain forever
	 * if the finger lifted off-screen (a real touch-up event lost). */
	fun releaseAll() {
		if (pointerNotes.isEmpty()) return
		pointerNotes.forEach { _, note -> onRelease(NOTE_KEYS[note]) }
		pointerNotes.clear()
		invalidate()
	}

	override fun onTouchEvent(event: MotionEvent): Boolean {
		when (event.actionMasked) {
			MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
				val i = event.actionIndex
				pressPointer(event.getPointerId(i), event.getX(i), event.getY(i))
			}
			MotionEvent.ACTION_MOVE -> {
				for (i in 0 until event.pointerCount) {
					movePointer(event.getPointerId(i), event.getX(i), event.getY(i))
				}
			}
			MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> {
				releasePointer(event.getPointerId(event.actionIndex))
			}
			MotionEvent.ACTION_CANCEL -> releaseAll()
		}
		return true
	}
}
