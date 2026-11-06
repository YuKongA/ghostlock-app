package com.ghostlock.app.core.flow

import com.ghostlock.app.domain.run.StageResult

/** Typed step result without arbitrary evidence maps. */
sealed interface StepResult {
    /** Step reached a terminal typed stage fact. */
    data class Done(val stage: StageResult) : StepResult

    /** Step requests another bounded attempt; reason is diagnostic only. */
    data class Retry(val reason: String) : StepResult

    /** Step reached a typed failure or unknown fact. */
    data class Failed(
        val stage: StageResult,
        val detail: String,
    ) : StepResult
}
