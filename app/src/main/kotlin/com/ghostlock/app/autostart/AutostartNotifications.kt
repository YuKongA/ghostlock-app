package com.ghostlock.app.autostart

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import androidx.core.app.NotificationCompat
import com.ghostlock.app.R
import com.ghostlock.app.ui.MainActivity

/**
 * Status and alert notifications for the boot auto-start. Without
 * POST_NOTIFICATIONS they are simply not shown; the in-app warning dialog
 * still reports a guard trip to the user.
 */
internal object AutostartNotifications {
    const val ServiceNotificationId = 0x474C

    private const val StatusChannel = "ghostlock_autostart_status"
    private const val AlertChannel = "ghostlock_autostart_alert"
    private const val GuardTrippedNotificationId = 0x474D

    fun ensureChannels(context: Context) {
        val manager = context.getSystemService(NotificationManager::class.java)
        manager.createNotificationChannel(
            NotificationChannel(
                StatusChannel,
                context.getString(R.string.autostart_channel_status),
                NotificationManager.IMPORTANCE_LOW,
            ),
        )
        manager.createNotificationChannel(
            NotificationChannel(
                AlertChannel,
                context.getString(R.string.autostart_channel_alert),
                NotificationManager.IMPORTANCE_DEFAULT,
            ),
        )
    }

    /** The ongoing notification backing the foreground service. */
    fun foreground(context: Context, message: String): Notification =
        build(context, StatusChannel, message, ongoing = true)

    /** The dismissible result notification left behind after the service stops. */
    fun result(context: Context, message: String, bigText: String?): Notification =
        build(context, AlertChannel, message, ongoing = false, bigText = bigText)

    /** Posted by the receiver when the boot-loop guard disables auto-start. */
    fun postGuardTripped(context: Context) {
        ensureChannels(context)
        notify(
            context,
            GuardTrippedNotificationId,
            build(
                context,
                AlertChannel,
                context.getString(R.string.autostart_disabled_message),
                ongoing = false,
                title = context.getString(R.string.autostart_disabled_title),
            ),
        )
    }

    fun notify(context: Context, id: Int, notification: Notification) {
        context.getSystemService(NotificationManager::class.java).notify(id, notification)
    }

    private fun build(
        context: Context,
        channel: String,
        message: String,
        ongoing: Boolean,
        title: String = context.getString(R.string.app_name),
        bigText: String? = null,
    ): Notification = NotificationCompat.Builder(context, channel)
        .setSmallIcon(R.drawable.ic_launcher_foreground)
        .setContentTitle(title)
        .setContentText(message)
        .apply { bigText?.let { setStyle(NotificationCompat.BigTextStyle().bigText(it)) } }
        .setOngoing(ongoing)
        .setOnlyAlertOnce(true)
        .setAutoCancel(true)
        .setForegroundServiceBehavior(NotificationCompat.FOREGROUND_SERVICE_IMMEDIATE)
        .setContentIntent(
            PendingIntent.getActivity(
                context,
                GuardTrippedNotificationId,
                Intent(context, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK),
                PendingIntent.FLAG_IMMUTABLE,
            ),
        )
        .build()
}
