package com.ghostlock.app.core.flow

/** step 执行上下文；attempt 从 1 开始。 */
data class StepContext(
    val name: String,
    val attempt: Int,
)
