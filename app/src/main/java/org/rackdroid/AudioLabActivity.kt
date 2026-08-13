package org.rackdroid

import android.app.Activity
import android.app.Application
import android.content.Intent
import android.os.Bundle
import android.os.Handler
import android.os.Looper

/**
 * v06.1 launcher trampoline for Audio Lab.
 *
 * The v06 implementation kept a second Activity on screen. On a real device
 * that made RackDroid's native audio stream disappear while the lab was open.
 * This Activity now exists only because Android static launcher shortcuts
 * target activities. It immediately brings MainActivity back to the front and
 * waits for that existing NativeActivity to resume. AudioLabDialog is then
 * attached to MainActivity itself, so the rack/audio lifetime is unchanged
 * while the controls and diagnostics are visible.
 */
class AudioLabActivity : Activity() {

	override fun onCreate(savedInstanceState: Bundle?) {
		super.onCreate(savedInstanceState)

		val app = application
		var delivered = false
		lateinit var callbacks: Application.ActivityLifecycleCallbacks
		callbacks = object : Application.ActivityLifecycleCallbacks {
			override fun onActivityResumed(activity: Activity) {
				if (delivered || activity !is MainActivity) return
				delivered = true
				app.unregisterActivityLifecycleCallbacks(this)
				// Wait until the resumed NativeActivity's decor is attached before
				// adding another window. The dialog belongs to MainActivity, not us.
				activity.window.decorView.post {
					if (!activity.isFinishing && !activity.isDestroyed)
						AudioLabDialog(activity).show()
				}
			}

			override fun onActivityCreated(activity: Activity, state: Bundle?) {}
			override fun onActivityStarted(activity: Activity) {}
			override fun onActivityPaused(activity: Activity) {}
			override fun onActivityStopped(activity: Activity) {}
			override fun onActivitySaveInstanceState(activity: Activity, state: Bundle) {}
			override fun onActivityDestroyed(activity: Activity) {}
		}
		app.registerActivityLifecycleCallbacks(callbacks)

		// Safety cleanup if MainActivity cannot be resumed for some unrelated
		// reason. Normal delivery unregisters immediately in onActivityResumed().
		Handler(Looper.getMainLooper()).postDelayed({
			if (!delivered) {
				delivered = true
				app.unregisterActivityLifecycleCallbacks(callbacks)
			}
		}, 15_000L)

		// Android's shortcut guidance recommends CLEAR_TOP + SINGLE_TOP for a
		// single-activity destination. MainActivity is also singleTask, so an
		// existing RackDroid task is reused rather than creating a second rack.
		startActivity(Intent(this, MainActivity::class.java).apply {
			addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP or Intent.FLAG_ACTIVITY_SINGLE_TOP)
		})
		finish()
	}

	companion object {
		// @JvmStatic keeps the exact v06 JNI class/method symbols, so v06.1 does
		// not touch audio_oboe.cpp or the native measurement implementation.
		@JvmStatic external fun nativeGetAudioStatus(): String
		@JvmStatic external fun nativeResetAudioMeasurements(): Boolean

		init {
			System.loadLibrary("rack_engine")
			System.loadLibrary("rackdroid")
		}
	}
}
