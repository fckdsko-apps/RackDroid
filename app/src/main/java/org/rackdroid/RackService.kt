package org.rackdroid

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.IBinder

/**
 * Foreground service keeping the process (engine + Oboe stream) alive while
 * the activity is in the background. Without it, Android's low-memory killer
 * treats the app as a cached process and reaps it aggressively — a synth
 * needs the same protection any media-playback app uses.
 */
class RackService : Service() {

	override fun onCreate() {
		super.onCreate()
		val channel = NotificationChannel(
			CHANNEL_ID, getString(R.string.service_channel_name), NotificationManager.IMPORTANCE_LOW)
		channel.description = getString(R.string.service_channel_description)
		getSystemService(NotificationManager::class.java).createNotificationChannel(channel)
	}

	override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
		if (intent?.action == ACTION_STOP) {
			// Finishing NativeActivity lets the native lifecycle save the patch
			// and tear down Oboe before the foreground protection disappears.
			MainActivity.requestStopFromNotification()
			stopForeground(STOP_FOREGROUND_REMOVE)
			stopSelf()
			return START_NOT_STICKY
		}
		val contentIntent = PendingIntent.getActivity(
			this, 0,
			Intent(this, MainActivity::class.java),
			PendingIntent.FLAG_IMMUTABLE)
		val stopIntent = PendingIntent.getService(
			this, 1,
			Intent(this, RackService::class.java).setAction(ACTION_STOP),
			PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE)
		val notification = Notification.Builder(this, CHANNEL_ID)
			.setContentTitle(getString(R.string.app_name))
			.setContentText(getString(R.string.service_running))
			.setSmallIcon(android.R.drawable.ic_media_play)
			.setContentIntent(contentIntent)
			.addAction(Notification.Action.Builder(
				android.graphics.drawable.Icon.createWithResource(
					this, android.R.drawable.ic_media_pause),
				getString(R.string.service_stop), stopIntent).build())
			.setOngoing(true)
			.build()
		startForeground(NOTIFICATION_ID, notification,
			ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK)
		// If Android ever kills this protected process, the native engine is
		// gone too. Restarting only the Service would show a ghost notification
		// with no synth behind it; the user should reopen the Activity instead.
		return START_NOT_STICKY
	}

	override fun onBind(intent: Intent?): IBinder? = null

	companion object {
		private const val CHANNEL_ID = "rackdroid.engine"
		private const val NOTIFICATION_ID = 1
		private const val ACTION_STOP = "org.rackdroid.action.STOP_BACKGROUND_AUDIO"

		fun start(context: Context) {
			context.startForegroundService(Intent(context, RackService::class.java))
		}

		fun stop(context: Context) {
			context.stopService(Intent(context, RackService::class.java))
		}
	}
}
