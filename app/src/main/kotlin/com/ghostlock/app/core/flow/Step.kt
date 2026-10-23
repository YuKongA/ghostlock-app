package com.ghostlock.app.core.flow

/** 最小 step 边界；lambda 即可，不要求类继承。 */
fun interface Step {
    suspend fun run(context: StepContext): StepResult
}
