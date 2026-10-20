package com.ghostlock.app.engine.pagecache.stage

import com.ghostlock.app.log.LogLevel
import com.ghostlock.app.log.log

/**
 * 现有 object log 的发射桥：格式/落盘/级别过滤全部交给 log；
 * 这里只把同一条消息同时交给 UI 的 onEvent。
 */
class StageLog(private val sink: (LogLevel, String) -> Unit = { _, _ -> }) {
    fun info(message: String) {
        log.info(message)
        sink(LogLevel.Info, message)
    }

    fun warn(message: String, reason: String) {
        log.warn(message, reason)
        sink(LogLevel.Warn, message + ": " + reason)
    }

    fun error(message: String, reason: String) {
        log.error(message, reason)
        sink(LogLevel.Error, message + ": " + reason)
    }

    fun debug(symbol: String, branch: String, vararg params: Pair<String, Any?>) {
        val text = symbol + "." + branch + params.joinToString("") { (key, value) -> " " + key + "=" + value }
        log.debug(text)
        sink(LogLevel.Debug, text)
    }

    fun verbose(symbol: String, vararg params: Pair<String, Any?>) {
        val text = symbol + params.joinToString("") { (key, value) -> " " + key + "=" + value }
        log.verbose(text)
        sink(LogLevel.Verbose, text)
    }
}

val silentStageLog: StageLog = StageLog()
