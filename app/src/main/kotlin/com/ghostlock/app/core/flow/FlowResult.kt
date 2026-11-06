package com.ghostlock.app.core.flow

/**
 * Internal sequential-flow assembly result.
 *
 * @property facts Backend facts returned through the public [Flow] contract.
 * @property steps Ordered typed step facts retained for diagnostics only.
 * @note No evidence map or text log participates in product success.
 */
data class FlowResult(
    val facts: BackendRunFacts,
    val steps: List<FlowStepRun>,
)
