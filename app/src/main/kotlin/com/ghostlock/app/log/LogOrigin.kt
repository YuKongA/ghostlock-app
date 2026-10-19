package com.ghostlock.app.log

/**
 * 一行的来源。终端用不同括号区分，避免把 Kotlin 编排、native 原语、shell 输出混成一种。
 *
 *  - [KOTLIN]：`<...>`
 *  - [NATIVE]：`[...]`
 *  - [SHELL]：`(...)`
 */
enum class LogOrigin {
    KOTLIN,
    NATIVE,
    SHELL,
}

/** 终端标签：括号形状由来源决定，括号内只有级别标记。 */
fun originLabel(origin: LogOrigin, level: LogLevel): String = when (origin) {
    LogOrigin.KOTLIN -> "<" + level.mark + "> "
    LogOrigin.NATIVE -> "[" + level.mark + "] "
    LogOrigin.SHELL -> "(" + level.mark + ") "
}

/** 启动后时间：秒两位（超过 99 自然加宽）·毫秒三位。 */
fun elapsedText(elapsedMs: Long): String {
    val seconds = elapsedMs / 1000L
    val millis = elapsedMs % 1000L
    return String.format(java.util.Locale.US, "%02d.%03d", seconds, millis)
}
