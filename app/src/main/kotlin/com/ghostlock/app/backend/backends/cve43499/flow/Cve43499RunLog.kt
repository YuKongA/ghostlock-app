package com.ghostlock.app.backend.backends.cve43499.flow

import com.ghostlock.app.core.flow.FlowProgress
import com.ghostlock.app.log.LogLevel
import com.ghostlock.app.log.log
import java.util.concurrent.atomic.AtomicLong

/**
 * 43499 run 的唯一日志发射点（UI 实时行 + logcat/FileSink）。
 *
 * 规则（task-101/103 / AGENTS §12）：
 * - UI 只显示 `>= Info`（Debug/Verbose 仅落 logcat/FileSink）；
 * - 同一事件只 emit 一次；`progress.onLine` 与 log 门面 **分别** runCatching，互不吞噬；
 * - 前置 sanitize：仅 ASCII、无控制字符、整行（含前缀）≤512B；超限截断并计数；
 * - Info 只给里程碑（无参数细节）；细节走 Debug；Warn/Error 用 `log.warn/error(msg, reason)`；
 * - 日志异常绝不改变 Flow 控制流/StepResult；PI 窗口内禁止调用本对象。
 */
internal object Cve43499RunLog {
    /** logcat/FileSink 标签（UI 行带 `[43499]` 前缀）。 */
    const val TAG: String = "Ghostlock43499"

    /** 单行硬上限（含前缀，ASCII 字节数）。 */
    const val MAX_LINE_BYTES: Int = 512

    private const val PREFIX: String = "[43499] "
    private val dropped = AtomicLong()
    private val truncated = AtomicLong()
    private val sanitized = AtomicLong()

    internal fun droppedLines(): Long = dropped.get()

    internal fun truncatedLines(): Long = truncated.get()

    internal fun sanitizedLines(): Long = sanitized.get()

    internal fun resetCounters() {
        dropped.set(0)
        truncated.set(0)
        sanitized.set(0)
    }

    /** Info 里程碑（无参数细节）。 */
    fun info(progress: FlowProgress, message: String) = emit(progress, LogLevel.Info, message)

    /** Debug 分支/细节（不进 UI）。 */
    fun debug(progress: FlowProgress, message: String) = emit(progress, LogLevel.Debug, message)

    /** Warn：可降级缺失/分支；reason 必须非空且不与 message 同义。 */
    fun warn(progress: FlowProgress, message: String, reason: String) =
        emit(progress, LogLevel.Warn, message, reason)

    /** Error：无回退失败；reason 必须非空且不与 message 同义。 */
    fun error(progress: FlowProgress, message: String, reason: String) =
        emit(progress, LogLevel.Error, message, reason)

    /**
     * 发一行事实日志（内部/兼容入口）。
     *
     * @param progress UI 进度 seam（可为 NONE）。
     * @param level 级别；Warn/Error 必须给 reason。
     * @param message 单行正文。
     * @param reason Warn/Error 的单一理由；其它级别忽略。
     */
    fun emit(progress: FlowProgress, level: LogLevel, message: String, reason: String? = null) {
        val normalizedReason = reason?.trim().orEmpty()
        if ((level == LogLevel.Warn || level == LogLevel.Error) && normalizedReason.isEmpty()) {
            dropped.incrementAndGet()
            return
        }
        val isProblem = level == LogLevel.Warn || level == LogLevel.Error
        val safeMessage = sanitize(message) ?: return
        val safeReason = if (isProblem) (sanitize(normalizedReason) ?: "unspecified") else ""
        val body = sanitize(if (isProblem) safeMessage + ": " + safeReason else safeMessage) ?: return
        val uiLine = PREFIX + body
        /* UI 只显示 Info 及以上：Debug/Verbose 仅 logcat/FileSink。 */
        if (level.ordinal >= LogLevel.Info.ordinal) {
            runCatching { progress.onLine(uiLine) }
        }
        /* 两个 sink 分别隔离；Warn/Error 走正式 log.warn/log.error API。 */
        runCatching {
            when (level) {
                LogLevel.Verbose -> log.invoke(LogLevel.Verbose, TAG, body)
                LogLevel.Debug -> log.invoke(LogLevel.Debug, TAG, body)
                LogLevel.Info -> log.invoke(LogLevel.Info, TAG, body)
                LogLevel.Warn -> log.warn(TAG, safeMessage, safeReason)
                LogLevel.Error -> log.error(TAG, safeMessage, safeReason)
            }
        }
    }

    /**
     * 清洗一行：替换非 ASCII/控制字符；超 512B 截断。
     *
     * @return 清洗后的正文；null=空白行（丢弃）。
     */
    internal fun sanitize(message: String): String? {
        val trimmed = message.trim()
        if (trimmed.isEmpty()) {
            dropped.incrementAndGet()
            return null
        }
        val builder = StringBuilder(trimmed.length)
        var changed = false
        for (ch in trimmed) {
            val code = ch.code
            if (code == 0x09 || code == 0x0A || code == 0x0D) {
                builder.append(' ')
                changed = true
            } else if (code < 0x20 || code == 0x7F || code > 0x7E) {
                builder.append('?')
                changed = true
            } else {
                builder.append(ch)
            }
        }
        if (changed) sanitized.incrementAndGet()
        val maxBody = (MAX_LINE_BYTES - PREFIX.length).coerceAtLeast(1)
        if (builder.length > maxBody) {
            truncated.incrementAndGet()
            return builder.substring(0, maxBody)
        }
        return builder.toString()
    }
}
