package com.ghostlock.app.core.flow

/** Backend stages that a Flow may own directly. */
enum class FlowStage {
    BACKEND_PRIMITIVE,
    ROOT_SCRIPT_DISPATCH,
}

/** One named physical backend operation, its exact fact ownership, and bounded retry/timing policy. */
data class FlowStep(
    val name: String,
    val produces: Set<FlowStage>,
    val retry: RetryPolicy = RetryPolicy(),
    val timing: StepTiming = StepTiming(),
    val step: Step,
) {
    init {
        require(name.isNotBlank()) { "step name must not be blank" }
        require(produces.isNotEmpty()) { "step must produce at least one stage fact" }
    }
}
