package com.ghostlock.app.core.flow

/** 一次 step 调用的结果；Retry 只重试当前 step。 */
sealed interface StepResult {
    data class Done(val evidence: Map<String, String> = emptyMap()) : StepResult
    data class Retry(val reason: String) : StepResult
    data class Failed(
        val reason: String,
        val evidence: Map<String, String> = emptyMap(),
    ) : StepResult
}
