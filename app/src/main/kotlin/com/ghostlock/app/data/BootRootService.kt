package com.ghostlock.app.data

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Intent
import android.os.IBinder
import android.util.Log
import com.ghostlock.app.GhostlockApplication
import com.ghostlock.app.R
import com.ghostlock.app.domain.model.ShizukuStatus
import com.ghostlock.app.domain.usecase.RunExploitUseCase
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

class BootRootService : Service() {
    private var runJob: Job? = null

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (runJob != null) return START_NOT_STICKY

        val channelName = getString(R.string.auto_root_on_boot)
        getSystemService(NotificationManager::class.java).createNotificationChannel(
            NotificationChannel(ChannelId, channelName, NotificationManager.IMPORTANCE_LOW),
        )
        startForeground(
            NotificationId,
            Notification.Builder(this, ChannelId)
                .setContentTitle(getString(R.string.auto_root_on_boot_running))
                .setSmallIcon(android.R.drawable.stat_notify_sync)
                .setOngoing(true)
                .build(),
        )

        runJob = CoroutineScope(Dispatchers.IO).launch {
            try {
                delay(BootSettleDelayMillis)
                runAfterBoot()
            } catch (error: CancellationException) {
                throw error
            } catch (error: Exception) {
                Log.e(Tag, "Automatic root failed", error)
            } finally {
                stopSelf()
            }
        }
        return START_NOT_STICKY
    }

    override fun onDestroy() {
        runJob?.cancel()
        super.onDestroy()
    }

    private suspend fun runAfterBoot() {
        val app = application as GhostlockApplication
        val settings = app.bootRootSettings
        val bootCount = settings.currentBootCount() ?: return
        if (!settings.shouldRun(bootCount)) return

        val repository = app.createRepository(silentManagerLaunch = true)
        try {
            val snapshot = repository.snapshot()
            if (!snapshot.kernelSupported) {
                Log.w(Tag, "Skipping automatic root: no supported kernel profile")
                return
            }
            if (snapshot.shizukuEnabled && snapshot.shizukuStatus != ShizukuStatus.READY) {
                Log.w(Tag, "Skipping automatic root: Shizuku is unavailable")
                return
            }
            val pair = snapshot.cpuPairs.getOrNull(snapshot.selectedCpuPair) ?: return
            if (snapshot.forceAttackTest) {
                Log.w(Tag, "Skipping automatic root: force attack test is enabled")
                return
            }
            if (!settings.begin(bootCount)) return

            // Leave the marker set if the attempt is interrupted.
            val result = RunExploitUseCase(repository)(pair, snapshot.shizukuEnabled) {}
            settings.finish(bootCount)
            Log.i(Tag, "Automatic root completed with exit code $result")
        } finally {
            repository.close()
        }
    }

    private companion object {
        const val Tag = "GhostLockBoot"
        const val ChannelId = "boot_root"
        const val NotificationId = 1
        const val BootSettleDelayMillis = 20_000L
    }
}
