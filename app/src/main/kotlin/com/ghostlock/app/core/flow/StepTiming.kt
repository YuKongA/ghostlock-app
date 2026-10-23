package com.ghostlock.app.core.flow

/**
 * step 的时间参数。
 *
 * preDelayMs / postDelayMs 在 step 外部执行；PI/futex 关键窗口内不得调用 delay。
 * timeoutMs=0 表示不设超时。
 */
data class StepTiming(
    val preDelayMs: Long = 0L,
    val postDelayMs: Long = 0L,
    val timeoutMs: Long = 0L,
) {
    init {
        require(preDelayMs >= 0L) { "preDelayMs must be >= 0" }
        require(postDelayMs >= 0L) { "postDelayMs must be >= 0" }
        require(timeoutMs >= 0L) { "timeoutMs must be >= 0" }
    }
}
