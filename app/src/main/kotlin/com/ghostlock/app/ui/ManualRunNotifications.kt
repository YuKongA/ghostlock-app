package com.ghostlock.app.ui

import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import androidx.core.app.NotificationCompat
import com.ghostlock.app.R
import com.ghostlock.app.boot.ExploitRunLock
import com.ghostlock.app.data.BootAutoRootPreferences

/** Silent shade notifications while the user taps Run in the main app. */
object ManualRunNotifications {
    const val CHANNEL_ID = "ghostlock_manual_run"
    const val PROGRESS_ID = 1003
    const val RESULT_ID = 1004

    fun ensureChannel(context: Context) {
        val manager = context.getSystemService(NotificationManager::class.java) ?: return
        manager.createNotificationChannel(
            NotificationChannel(
                CHANNEL_ID,
                context.getString(R.string.manual_run_channel_name),
                NotificationManager.IMPORTANCE_LOW,
            ).apply {
                description = context.getString(R.string.manual_run_channel_desc)
                setShowBadge(false)
                setSound(null, null)
                enableVibration(false)
            },
        )
    }

    fun showProgress(context: Context, line: String) {
        val prefs = BootAutoRootPreferences(context)
        if (!prefs.notifyProgressEnabled) return
        val manager = context.getSystemService(NotificationManager::class.java) ?: return
        ensureChannel(context)
        val title = context.getString(R.string.session_running)
        val text = when {
            !prefs.notifyProgressDetailed -> title
            line.isBlank() -> title
            else -> line.trim().take(100)
        }
        val notification = NotificationCompat.Builder(context, CHANNEL_ID)
            .setSmallIcon(R.drawable.ic_launcher_foreground)
            .setContentTitle(title)
            .setContentText(text)
            .setOngoing(true)
            .setOnlyAlertOnce(true)
            .setSilent(true)
            .setPriority(NotificationCompat.PRIORITY_LOW)
            .build()
        manager.notify(PROGRESS_ID, notification)
    }

    fun dismissProgress(context: Context) {
        context.getSystemService(NotificationManager::class.java)?.cancel(PROGRESS_ID)
    }

    fun showResult(context: Context, exitCode: Int) {
        dismissProgress(context)
        if (exitCode == ExploitRunLock.EXIT_BUSY) return
        val prefs = BootAutoRootPreferences(context)
        val success = exitCode == 0
        if (success && !prefs.notifyResultSuccess) return
        if (!success && !prefs.notifyResultFailure) return
        val manager = context.getSystemService(NotificationManager::class.java) ?: return
        ensureChannel(context)
        val titleRes = if (success) R.string.run_completed else R.string.boot_result_not_root_title
        val bodyRes = if (success) R.string.run_completed_summary else R.string.boot_result_not_root_body
        val openApp = PendingIntent.getActivity(
            context,
            0,
            Intent(context, MainActivity::class.java),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
        )
        val body = context.getString(bodyRes)
        val notification = NotificationCompat.Builder(context, CHANNEL_ID)
            .setSmallIcon(R.drawable.ic_launcher_foreground)
            .setContentTitle(context.getString(titleRes))
            .setContentText(body)
            .setStyle(NotificationCompat.BigTextStyle().bigText(body))
            .setContentIntent(openApp)
            .setAutoCancel(true)
            .setOnlyAlertOnce(true)
            .setSilent(true)
            .setPriority(NotificationCompat.PRIORITY_LOW)
            .build()
        manager.notify(RESULT_ID, notification)
    }
}
