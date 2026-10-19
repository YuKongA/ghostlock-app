package com.ghostlock.app.ui.screen.run

import android.content.Context
import com.ghostlock.app.engine.EngineLog
import com.ghostlock.app.engine.PageCacheEngine
import com.ghostlock.app.log.LogFile
import com.ghostlock.app.log.LogLevel
import com.ghostlock.app.log.LogOrigin
import com.ghostlock.app.log.NativeLogTailer
import com.ghostlock.app.ui.screen.module.backends.BackendSelection
import com.ghostlock.app.ui.screen.module.backends.Backends
import com.ghostlock.app.ui.screen.module.backends.EffectState
import com.ghostlock.app.ui.screen.module.backends.FinishConfig
import com.ghostlock.app.ui.screen.module.backends.FinishExecution
import com.ghostlock.app.ui.screen.module.backends.PayloadEffects

/**
 * 「执行」页的正式编排入口。
 *
 * 收尾在攻击之前准备（脚本文件 + 日志 memfd + knobs 补丁），攻击使用同一份补丁
 * 镜像；攻击结束后停止 drainer、删除脚本，日志保留。
 *
 * 调用方必须在 IO 线程运行本函数。
 */
object RunCoordinator {
    fun run(context: Context, onEvent: (LogOrigin, LogLevel, String) -> Unit) {
        fun info(message: String) = onEvent(LogOrigin.KOTLIN, LogLevel.Info, EngineLog.info(message))
        fun warn(message: String, reason: String) = onEvent(LogOrigin.KOTLIN, LogLevel.Warn, EngineLog.warn(message, reason))
        fun error(message: String, reason: String) = onEvent(LogOrigin.KOTLIN, LogLevel.Error, EngineLog.error(message, reason))
        fun debug(message: String, vararg params: String) = onEvent(LogOrigin.KOTLIN, LogLevel.Debug, EngineLog.debug(message, *params))
        fun verbose(message: String, vararg params: String) = onEvent(LogOrigin.KOTLIN, LogLevel.Verbose, EngineLog.verbose(message, *params))

        val backend = Backends.all.getOrNull(BackendSelection.index)
        val cve = backend?.cve ?: "none"
        debug("Run backend selected", cve)

        /* 收尾失败即中止：不带着未就绪的脚本触发攻击。 */
        val finish = try {
            FinishExecution.prepare(context)
        } catch (failure: FinishExecution.PrepException) {
            error("Finish preparation failed", failure.reason)
            return
        }
        verbose("Finish script", finish?.script?.absolutePath ?: "none")

        val tailer = NativeLogTailer.start(LogFile.path) { level, line ->
            onEvent(LogOrigin.NATIVE, level, line)
        }
        try {
            if (cve == "CVE-2026-43284") {
                runCatching {
                    PageCacheEngine.run(
                        context,
                        { level, line -> onEvent(LogOrigin.KOTLIN, level, line) },
                        finish?.patcher,
                    )
                }.onFailure { error("Run failed", it.message ?: "unknown") }
            } else {
                error("Path is not wired", cve)
            }
        } finally {
            tailer.stop()
            finish?.let { session ->
                when (runCatching { FinishExecution.awaitCompletion(5_000L) }.getOrNull()) {
                    true -> debug("Finish completion", "module-gone")
                    false -> warn("Finish completion unknown", "module-view")
                    null -> warn("Finish completion unavailable", "proc-unreadable")
                }
                runCatching { session.finish() }
                    .onFailure { warn("Finish cleanup failed", it.message ?: "failed") }
                session.logError?.let { reason -> error("Finish log degraded", reason) }
                verbose("Finish log", session.logFile.absolutePath)
            }
        }

        /* 效果探针只读内核视图与既有文件；没有观测到就记 unknown。 */
        val effects = runCatching { PayloadEffects.evaluate(FinishConfig.state) }
            .getOrDefault(emptyList())
        effects.forEach { report ->
            when (report.state) {
                EffectState.Verified -> debug("Effect verified", report.item, report.detail)
                EffectState.Unknown -> warn("Effect unknown", report.item)
                EffectState.Failed -> error("Effect failed", report.item)
            }
        }
        if (effects.isNotEmpty()) {
            when (PayloadEffects.overall(effects)) {
                EffectState.Verified -> info("Attack effects verified")
                EffectState.Unknown -> warn(
                    "Attack effects unknown",
                    effects.count { it.state == EffectState.Unknown }.toString(),
                )
                EffectState.Failed -> error(
                    "Attack effects failed",
                    effects.first { it.state == EffectState.Failed }.item,
                )
            }
        }
    }
}
