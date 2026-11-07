package com.ghostlock.app.core.flow

/** One immutable diagnostic record of an executed backend step. */
data class FlowStepRun(
    val name: String,
    val produces: Set<FlowStage>,
    val attempts: Int,
    val elapsedMs: Long,
    val facts: FlowStepFacts,
)
