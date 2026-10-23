package com.ghostlock.app.core.flow

/** 单步重试策略；只作用于当前 step。 */
data class RetryPolicy(
    val attempts: Int = 1,
    val delayMs: Long = 0L,
    val maxDelayMs: Long = 30_000L,
    val jitterMs: Long = 0L,
) {
    init {
        require(attempts >= 1) { "attempts must be >= 1" }
        require(delayMs >= 0L) { "delayMs must be >= 0" }
        require(maxDelayMs >= 0L) { "maxDelayMs must be >= 0" }
        require(jitterMs >= 0L) { "jitterMs must be >= 0" }
    }
}
