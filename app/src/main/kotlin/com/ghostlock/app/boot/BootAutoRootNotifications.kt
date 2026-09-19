package com.ghostlock.app.boot

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import androidx.core.app.NotificationCompat
import com.ghostlock.app.R
import com.ghostlock.app.ui.MainActivity

object BootAutoRootNotifications {
    const val CHANNEL_PROGRESS_ID = "boot_auto_root_progress"
    const val CHANNEL_RESULT_ID = "boot_auto_root_result"
    const val NOTIFICATION_PROGRESS_ID = 1001
    const val NOTIFICATION_RESULT_ID = 1002

    fun ensureChannels(context: Context) {
        val manager = context.getSystemService(NotificationManager::class.java) ?: return
        manager.createNotificationChannel(
            NotificationChannel(
                CHANNEL_PROGRESS_ID,
                context.getString(R.string.boot_auto_root_channel_name),
                NotificationManager.IMPORTANCE_LOW,
            ).apply {
                description = context.getString(R.string.boot_auto_root_channel_desc)
                setShowBadge(false)
            },
        )
        manager.createNotificationChannel(
            NotificationChannel(
                CHANNEL_RESULT_ID,
                context.getString(R.string.boot_auto_root_result_channel_name),
                NotificationManager.IMPORTANCE_DEFAULT,
            ).apply {
                description = context.getString(R.string.boot_auto_root_result_channel_desc)
            },
        )
    }

    fun buildProgress(context: Context, text: String): Notification {
        val safeText = text.ifBlank { context.getString(R.string.boot_auto_root_running) }
        return NotificationCompat.Builder(context, CHANNEL_PROGRESS_ID)
            .setSmallIcon(R.drawable.ic_launcher_foreground)
            .setContentTitle(context.getString(R.string.boot_auto_root_notification_title))
            .setContentText(safeText)
            .setOngoing(true)
            .setOnlyAlertOnce(true)
            .setSilent(true)
            .build()
    }

    fun showResult(context: Context, result: BootRunResult) {
        if (result is BootRunResult.Skipped && result.reason == SkipReason.Disabled) return
        val manager = context.getSystemService(NotificationManager::class.java) ?: return
        ensureChannels(context)
        val (titleRes, bodyRes) = when (result) {
            BootRunResult.Success -> R.string.boot_result_success_title to R.string.boot_result_success_body
            BootRunResult.FailedNotRoot -> R.string.boot_result_not_root_title to R.string.boot_result_not_root_body
            BootRunResult.StoppedSafely -> R.string.boot_result_safe_stop_title to R.string.boot_result_safe_stop_body
            is BootRunResult.Skipped -> when (result.reason) {
                SkipReason.UnsupportedKernel -> R.string.boot_result_skipped_title to R.string.boot_result_skipped_unsupported_body
                SkipReason.NoCpuPair -> R.string.boot_result_skipped_title to R.string.boot_result_skipped_cpu_body
                SkipReason.Disabled -> return
            }
        }
        val openApp = PendingIntent.getActivity(
            context,
            0,
            Intent(context, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
        )
        val notification = NotificationCompat.Builder(context, CHANNEL_RESULT_ID)
            .setSmallIcon(R.drawable.ic_launcher_foreground)
            .setContentTitle(context.getString(titleRes))
            .setContentText(context.getString(bodyRes))
            .setStyle(NotificationCompat.BigTextStyle().bigText(context.getString(bodyRes)))
            .setContentIntent(openApp)
            .setAutoCancel(true)
            .setOnlyAlertOnce(true)
            .build()
        manager.notify(NOTIFICATION_RESULT_ID, notification)
    }
}
