package com.ghostlock.app.boot

import android.app.Service
import android.content.Context
import android.content.Intent
import android.os.IBinder
import com.ghostlock.app.R
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

class BootAutoRootService : Service() {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        val beforeUnlock = intent?.getBooleanExtra(EXTRA_BEFORE_UNLOCK, false) == true
        try {
            BootAutoRootNotifications.ensureChannels(this)
            startForeground(
                BootAutoRootNotifications.NOTIFICATION_PROGRESS_ID,
                BootAutoRootNotifications.buildProgress(
                    this,
                    getString(
                        if (beforeUnlock) {
                            R.string.boot_auto_root_running_locked
                        } else {
                            R.string.boot_auto_root_running
                        },
                    ),
                ),
            )
        } catch (_: Throwable) {
            stopSelf()
            return START_NOT_STICKY
        }
        scope.launch {
            var result: BootRunResult = BootRunResult.StoppedSafely
            try {
                result = withContext(Dispatchers.IO) {
                    BootAutoRootRunner.runIfConfigured(this@BootAutoRootService, beforeUnlock) { line ->
                        try {
                            val summary = line.take(100)
                            startForeground(
                                BootAutoRootNotifications.NOTIFICATION_PROGRESS_ID,
                                BootAutoRootNotifications.buildProgress(this@BootAutoRootService, summary),
                            )
                        } catch (_: Throwable) {
                        }
                    }
                }
            } catch (error: CancellationException) {
                throw error
            } catch (_: Throwable) {
                result = BootRunResult.StoppedSafely
            } finally {
                try {
                    BootAutoRootNotifications.showResult(this@BootAutoRootService, result)
                } catch (_: Throwable) {
                }
                try {
                    stopForeground(STOP_FOREGROUND_REMOVE)
                } catch (_: Throwable) {
                }
                stopSelf()
            }
        }
        return START_NOT_STICKY
    }

    override fun onDestroy() {
        scope.cancel()
        super.onDestroy()
    }

    companion object {
        const val EXTRA_BEFORE_UNLOCK = "before_unlock"

        fun start(context: Context, beforeUnlock: Boolean) {
            val intent = Intent(context, BootAutoRootService::class.java)
                .putExtra(EXTRA_BEFORE_UNLOCK, beforeUnlock)
            context.startForegroundService(intent)
        }
    }
}
