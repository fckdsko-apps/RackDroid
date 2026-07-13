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
			CHANNEL_ID, "RackDroid", NotificationManager.IMPORTANCE_LOW)
		channel.description = "Synth engine running"
		getSystemService(NotificationManager::class.java).createNotificationChannel(channel)
	}

	override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
		val contentIntent = PendingIntent.getActivity(
			this, 0,
			Intent(this, MainActivity::class.java),
			PendingIntent.FLAG_IMMUTABLE)
		val notification = Notification.Builder(this, CHANNEL_ID)
			.setContentTitle(getString(R.string.app_name))
			.setContentText("Synth engine running")
			.setSmallIcon(android.R.drawable.ic_media_play)
			.setContentIntent(contentIntent)
			.setOngoing(true)
			.build()
		startForeground(NOTIFICATION_ID, notification,
			ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK)
		return START_STICKY
	}

	override fun onBind(intent: Intent?): IBinder? = null

	companion object {
		private const val CHANNEL_ID = "rackdroid.engine"
		private const val NOTIFICATION_ID = 1

		fun start(context: Context) {
			context.startForegroundService(Intent(context, RackService::class.java))
		}

		fun stop(context: Context) {
			context.stopService(Intent(context, RackService::class.java))
		}
	}
}
