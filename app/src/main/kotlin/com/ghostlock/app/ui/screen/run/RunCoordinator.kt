package com.ghostlock.app.ui.screen.run

import android.content.Context
import com.ghostlock.app.log.LogLevel
import com.ghostlock.app.log.LogOrigin
import com.ghostlock.app.log.LogFile
import com.ghostlock.app.log.NativeLogTailer
import com.ghostlock.app.tempui.TypedBackendProbe
import com.ghostlock.app.ui.screen.module.backends.BackendSelection
import com.ghostlock.app.ui.screen.module.backends.Backends
import com.ghostlock.app.ui.screen.module.backends.FinishExecution

/**
 * 「执行」页的正式编排入口。
 *
 * 当前接线：43284 走 [TypedBackendProbe.run]（原 tempui 的完整链），
 * 随后若有收尾脚本则交给 [FinishExecution] 执行；其他 backend 暂只报未接线。
 * 调用方必须在 IO 线程运行本函数。
 */
object RunCoordinator {
    fun run(context: Context, onEvent: (LogOrigin, LogLevel, String) -> Unit) {
        fun emit(line: String) = onEvent(LogOrigin.KOTLIN, LogLevel.Info, line)
        fun emitVerbose(line: String) = onEvent(LogOrigin.KOTLIN, LogLevel.Verbose, line)
        fun emitShell(level: LogLevel, line: String) = onEvent(LogOrigin.SHELL, level, line)
        val backend = Backends.all.getOrNull(BackendSelection.index)
        val cve = backend?.cve ?: "none"
        emitVerbose("[run] start backend=" + cve)
        val tailer = NativeLogTailer.start(LogFile.path) { level, line ->
            onEvent(LogOrigin.NATIVE, level, line)
        }
        try {
            if (cve == "CVE-2026-43284") {
                runCatching { TypedBackendProbe.run(context) { level, line -> onEvent(LogOrigin.KOTLIN, level, line) } }
                    .onFailure { emit("[run] failed: " + (it.message ?: "unknown")) }
            } else {
                emit("[run] orchestration not wired for " + cve)
            }
        } finally {
            tailer.stop()
        }
        if (false) {
            emit("[run] orchestration not wired for " + cve)
        }
        val script = FinishExecution.pendingScript(context)
        if (script.isFile) {
            emitVerbose("[finish] start " + script.absolutePath + " bytes=" + script.length())
            /* 收尾脚本内容也进日志：只读文本，逐行以 verbose 输出（导出文件始终保留）。 */
            runCatching { script.readLines() }.getOrDefault(emptyList()).forEach { line ->
                emitVerbose("[finish] script| " + line)
            }
            FinishExecution.run(context, ::emitShell)
        } else {
            emit("[finish] no pending script")
        }
    }
}
