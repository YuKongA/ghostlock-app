package com.ghostlock.app.boot

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import androidx.core.app.NotificationCompat
import com.ghostlock.app.R
import com.ghostlock.app.data.BootAutoRootPreferences
import com.ghostlock.app.ui.MainActivity

object BootAutoRootNotifications {
    const val CHANNEL_PROGRESS_ID = "boot_auto_root_progress"
    const val CHANNEL_RESULT_ID = "boot_auto_root_result"
    const val NOTIFICATION_PROGRESS_ID = 1001
    const val NOTIFICATION_RESULT_ID = 1002

    fun ensureChannels(context: Context) {
        val prefs = BootAutoRootPreferences(context)
        val manager = context.getSystemService(NotificationManager::class.java) ?: return
        val progressImportance = if (prefs.notifyProgressEnabled) {
            NotificationManager.IMPORTANCE_DEFAULT
        } else {
            NotificationManager.IMPORTANCE_MIN
        }
        manager.createNotificationChannel(
            NotificationChannel(
                CHANNEL_PROGRESS_ID,
                context.getString(R.string.boot_auto_root_channel_name),
                progressImportance,
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

    fun buildProgress(context: Context, line: String): Notification {
        val prefs = BootAutoRootPreferences(context)
        val title = context.getString(R.string.session_running)
        val body = when {
            !prefs.notifyProgressEnabled -> title
            !prefs.notifyProgressDetailed -> title
            line.isBlank() -> title
            else -> line.trim().take(100)
        }
        return NotificationCompat.Builder(context, CHANNEL_PROGRESS_ID)
            .setSmallIcon(R.drawable.ic_launcher_foreground)
            .setContentTitle(title)
            .setContentText(body)
            .setOngoing(true)
            .setOnlyAlertOnce(true)
            .setSilent(!prefs.notifyProgressEnabled)
            .build()
    }

    fun showResult(context: Context, result: BootRunResult) {
        val prefs = BootAutoRootPreferences(context)
        if (!prefs.shouldShowResult(result)) return
        if (result is BootRunResult.Skipped && result.reason == SkipReason.Disabled) return
        val manager = context.getSystemService(NotificationManager::class.java) ?: return
        ensureChannels(context)
        val (titleRes, bodyRes, detail) = when (result) {
            BootRunResult.Success -> Triple(R.string.run_completed, R.string.run_completed_summary, null)
            BootRunResult.FailedNotRoot -> Triple(R.string.boot_result_not_root_title, R.string.boot_result_not_root_body, null)
            is BootRunResult.StoppedSafely -> Triple(
                R.string.boot_result_safe_stop_title,
                R.string.boot_result_safe_stop_body,
                result.detail,
            )
            is BootRunResult.Skipped -> when (result.reason) {
                SkipReason.UnsupportedKernel -> Triple(
                    R.string.boot_result_skipped_title,
                    R.string.boot_result_skipped_unsupported_body,
                    null,
                )
                SkipReason.NoCpuPair -> Triple(
                    R.string.boot_result_skipped_title,
                    R.string.boot_result_skipped_cpu_body,
                    null,
                )
                SkipReason.RunInProgress -> Triple(
                    R.string.boot_result_skipped_title,
                    R.string.boot_result_skipped_busy_body,
                    null,
                )
                SkipReason.Disabled -> return
            }
        }
        val body = context.getString(bodyRes)
        val fullBody = if (detail.isNullOrBlank()) {
            body
        } else {
            context.getString(R.string.boot_result_detail_suffix, detail, body)
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
            .setContentText(fullBody)
            .setStyle(NotificationCompat.BigTextStyle().bigText(fullBody))
            .setContentIntent(openApp)
            .setAutoCancel(true)
            .setOnlyAlertOnce(true)
            .build()
        manager.notify(NOTIFICATION_RESULT_ID, notification)
    }
}
