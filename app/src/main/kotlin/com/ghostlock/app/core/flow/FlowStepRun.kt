package com.ghostlock.app.core.flow

/** 单个 step 的运行记录；用于日志/证据，不参与控制流。 */
data class FlowStepRun(
    val name: String,
    val attempts: Int,
    val elapsedMs: Long,
    val result: StepResult,
)
