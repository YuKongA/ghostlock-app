package com.ghostlock.app.attack

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Intent
import android.os.Bundle
import android.os.IBinder
import android.os.Process
import android.os.ResultReceiver
import com.ghostlock.app.R
import com.ghostlock.app.engine.EngineLog
import com.ghostlock.app.log.LogLevel
import com.ghostlock.app.log.LogOrigin
import com.ghostlock.app.ui.screen.run.RunCoordinator

/**
 * 攻击执行进程（manifest 中 `android:process=":attack"`）。
 *
 * 主进程只负责 UI 与日志显示；真正的 JNI 链、su 进程与 relay 窗口都在这里。
 * 紧急中止 = 杀掉本进程（同 uid），由 OS 回收线程/fd。
 */
class AttackService : Service() {
    companion object {
        const val ACTION_START = "com.ghostlock.app.attack.START"
        const val ACTION_ABORT = "com.ghostlock.app.attack.ABORT"
        const val EXTRA_RECEIVER = "receiver"
        const val CODE_EVENT = 1
        const val CODE_DONE = 2
        const val CODE_PID = 3
        const val KEY_PID = "pid"
        const val KEY_ORIGIN = "origin"
        const val KEY_LEVEL = "level"
        const val KEY_LINE = "line"
        const val KEY_EXIT = "exit"
        private const val CHANNEL_ID = "ghostlock-attack"
        private const val NOTIFICATION_ID = 0x4328
    }

    @Volatile
    private var worker: Thread? = null


    override fun onCreate() {
        super.onCreate()
        val manager = getSystemService(NotificationManager::class.java)
        manager.createNotificationChannel(
            NotificationChannel(CHANNEL_ID, getString(R.string.attack_notification_channel), NotificationManager.IMPORTANCE_LOW),
        )
        startForeground(
            NOTIFICATION_ID,
            Notification.Builder(this, CHANNEL_ID)
                .setContentTitle(getString(R.string.attack_notification_title))
                .setContentText(getString(R.string.attack_notification_text))
                .setSmallIcon(R.drawable.ic_launcher_foreground)
                .build(),
        )
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_ABORT -> abort(intent)
            ACTION_START -> start(intent)
            else -> Unit
        }
        return START_NOT_STICKY
    }

    private fun start(intent: Intent) {
        if (worker?.isAlive == true) return
        @Suppress("DEPRECATION")
        val receiver = intent.getParcelableExtra<ResultReceiver>(EXTRA_RECEIVER)
        receiver?.send(CODE_PID, Bundle().apply { putInt(KEY_PID, Process.myPid()) })
        worker = Thread({
            com.ghostlock.app.ui.screen.module.backends.FinishExecution.cleanStale(applicationContext)
            fun record(origin: LogOrigin, level: LogLevel, line: String) {
                emit(receiver, origin, level, line)
            }
            val outcome = runCatching {
                RunCoordinator.run(applicationContext) { origin, level, line -> record(origin, level, line) }
            }
            outcome.onFailure { error ->
                record(LogOrigin.KOTLIN, LogLevel.Error, EngineLog.error("Attack failed", error.message ?: "failed"))
            }
            /* 开机重试计数属流程外状态：成功跑完才清零，panic 时保留。 */
            if (outcome.isSuccess) BootSettings.clearAttempts(this)
            receiver?.send(CODE_DONE, Bundle.EMPTY)
            stopSelf()
        }, "ghostlock-attack").apply { start() }
    }

    private fun abort(intent: Intent) {
        @Suppress("DEPRECATION")
        val receiver = intent.getParcelableExtra<ResultReceiver>(EXTRA_RECEIVER)
        val target = Process.myPid()
        emit(receiver, LogOrigin.KOTLIN, LogLevel.Warn, EngineLog.warn("Attack abort requested", target.toString()))
        PidRegistry.killDescendants(target)
        worker?.interrupt()
        stopForeground(STOP_FOREGROUND_REMOVE)
        Process.killProcess(Process.myPid())
    }

    private fun emit(receiver: ResultReceiver?, origin: LogOrigin, level: LogLevel, line: String) {
        receiver?.send(CODE_EVENT, Bundle().apply {
            putString(KEY_ORIGIN, origin.name)
            putString(KEY_LEVEL, level.name)
            putString(KEY_LINE, line)
        })
    }

    override fun onBind(intent: Intent?): IBinder? = null
}
