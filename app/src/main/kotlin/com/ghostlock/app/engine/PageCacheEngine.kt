package com.ghostlock.app.engine

import android.content.Context
import com.ghostlock.app.engine.pagecache.TypedBackendProbe
import com.ghostlock.app.log.LogLevel

/**
 * 43284 页缓存路径的生产入口。
 *
 * 43284 业务数据面由 Assembled43284 内部的 PageCacheAccess 适配器执行；控制面仍由页缓存链执行。
 */
object PageCacheEngine {
    /**
     * @param context 应用上下文。
     * @param onEvent 逐行事件回调。
     * @param modulePatcher 写页缓存前对 `.ko` 镜像做的内存补丁（脚本路径）；null 表示不补丁。
     */
    fun run(
        context: Context,
        onEvent: (LogLevel, String) -> Unit,
        modulePatcher: ((ByteArray) -> ByteArray)? = null,
    ): List<String> {
        emit(onEvent, LogLevel.Info, EngineLog.info("Page cache engine started"))
        val lines = TypedBackendProbe.run(context, { level, line ->
            when (level) {
                LogLevel.Info -> emit(onEvent, LogLevel.Debug, EngineLog.debug("Page cache detail", line))
                LogLevel.Warn -> emit(onEvent, LogLevel.Warn, EngineLog.warn("Page cache step degraded", reasonOf(line)))
                LogLevel.Error -> emit(onEvent, LogLevel.Error, EngineLog.error("Page cache step failed", reasonOf(line)))
                LogLevel.Debug -> emit(onEvent, LogLevel.Debug, EngineLog.debug("Page cache detail", line))
                LogLevel.Verbose -> emit(onEvent, LogLevel.Verbose, EngineLog.verbose("Page cache detail", line))
            }
        }, modulePatcher)
        emit(onEvent, LogLevel.Info, EngineLog.info("Page cache engine finished"))
        return lines
    }

    private fun reasonOf(line: String): String {
        val afterRefusal = line.substringAfter("refusal=", "").trim()
        val candidate = if (afterRefusal.isNotEmpty()) afterRefusal else line.substringAfterLast(':').trim()
        return candidate.substringBefore(' ').ifBlank { "unknown" }
    }

    private fun emit(onEvent: (LogLevel, String) -> Unit, level: LogLevel, message: String) {
        onEvent(level, message)
    }
}
