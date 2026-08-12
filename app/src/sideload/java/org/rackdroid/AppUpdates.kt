package org.rackdroid

import android.app.Activity
import android.content.Intent

/**
 * Self-update is intentionally disabled in this sideload build.
 *
 * RackDroid's Android/module compatibility work can diverge from upstream,
 * so an unsolicited application update must never replace the installed APK.
 * APK updates remain a deliberate manual install by the user.
 */
object AppUpdates {
	const val SUPPORTED = false

	fun onStart(activity: Activity) {
		// Intentionally no automatic update check or opt-in prompt.
	}

	fun checkNow(activity: Activity) {
		// Intentionally disabled. SUPPORTED=false hides the UI entry point.
	}

	fun onNewIntent(activity: Activity, intent: Intent?) {
		// No updater installer session exists in this build.
	}
}
