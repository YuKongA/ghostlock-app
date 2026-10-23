package com.ghostlock.app.core.flow

/** 一个命名 step 及其重试/时序参数。 */
data class FlowStep(
    val name: String,
    val retry: RetryPolicy = RetryPolicy(),
    val timing: StepTiming = StepTiming(),
    val step: Step,
) {
    init {
        require(name.isNotBlank()) { "step name must not be blank" }
    }
}
