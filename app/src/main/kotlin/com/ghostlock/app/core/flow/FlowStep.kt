package com.ghostlock.app.core.flow

/** Backend stages that a Flow may own directly. */
enum class FlowStage {
    BACKEND_PRIMITIVE,
    ROOT_SCRIPT_DISPATCH,
}

/** One named backend stage and its bounded retry/timing policy. */
data class FlowStep(
    val name: String,
    val stage: FlowStage,
    val retry: RetryPolicy = RetryPolicy(),
    val timing: StepTiming = StepTiming(),
    val step: Step,
) {
    init {
        require(name.isNotBlank()) { "step name must not be blank" }
    }
}
