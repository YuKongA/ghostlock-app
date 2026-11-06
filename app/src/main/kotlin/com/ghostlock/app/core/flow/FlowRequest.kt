package com.ghostlock.app.core.flow

/**
 * One validated request view consumed by sequential step planners.
 *
 * @property input Uniform frozen backend input.
 * @note This wrapper carries no backend-specific fields and exists only to keep step planning explicit.
 */
data class FlowRequest(
    val input: BackendFlowInput,
)
