package com.ghostlock.app.core.flow

import com.ghostlock.app.domain.run.StageResult

/** One immutable diagnostic record of an executed backend step. */
data class FlowStepRun(
    val name: String,
    val stage: FlowStage,
    val attempts: Int,
    val elapsedMs: Long,
    val result: StageResult,
)
