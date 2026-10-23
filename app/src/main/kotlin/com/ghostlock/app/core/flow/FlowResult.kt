package com.ghostlock.app.core.flow

/** flow 的统一结果；不暴露 backend 内部节点。 */
sealed interface FlowResult {
    val steps: List<FlowStepRun>
    val evidence: Map<String, String>

    data class Ok(
        override val steps: List<FlowStepRun>,
        override val evidence: Map<String, String>,
    ) : FlowResult

    data class Failed(
        val reason: String,
        val failedStep: String?,
        override val steps: List<FlowStepRun>,
        override val evidence: Map<String, String>,
    ) : FlowResult
}
