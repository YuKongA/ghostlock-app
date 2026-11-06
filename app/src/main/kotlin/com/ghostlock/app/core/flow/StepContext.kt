package com.ghostlock.app.core.flow

/**
 * One sequential step invocation.
 *
 * @property input Uniform frozen backend input.
 * @property name Stable diagnostic name.
 * @property attempt One-based retry attempt.
 */
data class StepContext(
    val input: BackendFlowInput,
    val name: String,
    val attempt: Int,
)
