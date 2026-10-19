package com.ghostlock.app.attack

import android.content.Context
import android.content.Intent
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.ResultReceiver
import com.ghostlock.app.attack.PidRegistry
import androidx.core.content.ContextCompat
import com.ghostlock.app.log.LogLevel
import com.ghostlock.app.log.LogOrigin

/**
 * 主进程侧的攻入口：启动/中止 `:attack` 进程，并把日志与完成事件送回 UI。
 *
 * 中止走 ACTION_ABORT：服务先杀后代进程，再 `Process.killProcess(myPid())` 结束自己。
 */
object AttackClient {
    private var active: ResultReceiver? = null

    fun start(
        context: Context,
        onEvent: (LogOrigin, LogLevel, String) -> Unit,
        onDone: () -> Unit,
    ) {
        val receiver = object : ResultReceiver(Handler(Looper.getMainLooper())) {
            override fun onReceiveResult(resultCode: Int, resultData: Bundle?) {
                when (resultCode) {
                    AttackService.CODE_EVENT -> {
                        val origin = runCatching {
                            LogOrigin.valueOf(resultData?.getString(AttackService.KEY_ORIGIN) ?: LogOrigin.KOTLIN.name)
                        }.getOrDefault(LogOrigin.KOTLIN)
                        val level = runCatching {
                            LogLevel.valueOf(resultData?.getString(AttackService.KEY_LEVEL) ?: LogLevel.Info.name)
                        }.getOrDefault(LogLevel.Info)
                        onEvent(origin, level, resultData?.getString(AttackService.KEY_LINE).orEmpty())
                    }
                    AttackService.CODE_DONE -> onDone()
                }
            }
        }
        active = receiver
        val intent = Intent(context, AttackService::class.java)
            .setAction(AttackService.ACTION_START)
            .putExtra(AttackService.EXTRA_RECEIVER, receiver)
        ContextCompat.startForegroundService(context, intent)
    }

    /**
     * 中止：直接杀记录在 attack.pid 里的攻击进程（同 uid），并清理其后代。
     *
     * 不再拉起新的 :attack 进程来接收 abort，避免“abort requested 反复出现”。
     *
     * @param onAborted 确认进程消失（或超时）后在主线程回调。
     */
    fun abort(context: Context, onAborted: () -> Unit) {
        val pid = readAttackPid(context)
        if (pid > 0) {
            PidRegistry.killDescendants(pid)
            runCatching { android.system.Os.kill(pid, android.system.OsConstants.SIGKILL) }
                .onFailure { android.util.Log.w("GhostlockAttack", "kill " + pid + " failed", it) }
        }
        runCatching { java.io.File(context.filesDir, AttackService.PID_FILE).delete() }
        active = null
        awaitProcessGone(context, pid, System.currentTimeMillis() + 8_000L, onAborted)
    }

    private fun readAttackPid(context: Context): Int =
        runCatching {
            java.io.File(context.filesDir, AttackService.PID_FILE).readText().trim().toInt()
        }.getOrDefault(-1)

    private fun awaitProcessGone(context: Context, pid: Int, deadline: Long, onAborted: () -> Unit) {
        val handler = Handler(Looper.getMainLooper())
        fun check() {
            if (pid <= 0 || !java.io.File("/proc/" + pid).exists() || System.currentTimeMillis() >= deadline) {
                onAborted()
            } else {
                handler.postDelayed({ check() }, 120L)
            }
        }
        handler.postDelayed({ check() }, 120L)
    }

}
