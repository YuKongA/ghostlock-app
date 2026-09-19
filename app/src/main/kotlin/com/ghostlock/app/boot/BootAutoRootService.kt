package com.ghostlock.app.boot

import android.app.Service
import android.content.Context
import android.content.Intent
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import com.ghostlock.app.R
import com.ghostlock.app.data.BootAutoRootPreferences
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

class BootAutoRootService : Service() {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)
    private val mainHandler = Handler(Looper.getMainLooper())

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        val bootPrefs = BootAutoRootPreferences(this)
        try {
            BootAutoRootNotifications.ensureChannels(this)
            startForeground(
                BootAutoRootNotifications.NOTIFICATION_PROGRESS_ID,
                BootAutoRootNotifications.buildProgress(
                    this,
                    getString(R.string.session_running),
                ),
            )
        } catch (error: Throwable) {
            BootRunCoordinator.onServiceStartFailed()
            BootAutoRootPreferences(this).recordBootFailure(
                error.message?.take(120) ?: error.javaClass.simpleName,
            )
            stopSelf()
            return START_NOT_STICKY
        }
        scope.launch {
            var result: BootRunResult = BootRunResult.StoppedSafely()
            try {
                result = withContext(Dispatchers.IO) {
                    BootAutoRootRunner.runIfConfigured(this@BootAutoRootService) { line ->
                        if (!bootPrefs.notifyProgressDetailed) return@runIfConfigured
                        mainHandler.post {
                            try {
                                startForeground(
                                    BootAutoRootNotifications.NOTIFICATION_PROGRESS_ID,
                                    BootAutoRootNotifications.buildProgress(this@BootAutoRootService, line),
                                )
                            } catch (_: Throwable) {
                            }
                        }
                    }
                }
            } catch (error: CancellationException) {
                throw error
            } catch (error: Throwable) {
                val detail = error.message?.take(120) ?: error.javaClass.simpleName
                result = BootRunResult.StoppedSafely(detail)
            } finally {
                val prefs = BootAutoRootPreferences(this@BootAutoRootService)
                prefs.recordBootFinished(
                    when (result) {
                        BootRunResult.Success -> "Finished: run completed"
                        BootRunResult.FailedNotRoot -> "Finished: not rooted"
                        is BootRunResult.StoppedSafely -> "Finished: stopped safely"
                        is BootRunResult.Skipped -> "Finished: skipped (${result.reason})"
                    },
                )
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
        fun start(context: Context) {
            context.startForegroundService(Intent(context, BootAutoRootService::class.java))
        }
    }
}
