package com.ghostlock.app.engine.ktstep

import com.ghostlock.app.core.steps.StepId
import com.ghostlock.app.core.steps.StepInputs

/** 一步的 Kotlin 输入：目标、写形状、值；页由引擎按 plan 的 layout/page inputs 构建。 */
data class KtStep(
    val step: StepId,
    val target: Long,
    val mode: WriteMode,
    val preserveChild: Boolean,
    val value: Long = 0L,
)

/**
 * 一次 kt-step 运行的计划：W1/W2/W3 + 页/凭据布局 + 尝试预算。
 *
 * 所有数值由调用方注入；这里不读 profile，也不猜默认值。
 */
data class KtStepPlan(
    val steps: Map<StepId, KtStep>,
    val layoutInputs: LayoutInputs,
    val pageInputs: PayloadPageInputs,
    val pageSize: Int,
    val attempts: Int = 1,
    val settleMs: Long = 0L,
    val expectedUid: Long = 0L,
) {
    /** 转成 PipelineEngine/StepPlans 的输入；地址/值/偏移都来自本计划。 */
    fun stepInputs(): StepInputs {
        val w1 = steps.getValue(StepId.W1)
        val w2 = steps.getValue(StepId.W2)
        val w3 = steps.getValue(StepId.W3)
        return StepInputs(
            attempts = attempts,
            settleMs = settleMs,
            selinuxAddress = w1.target.toULong(),
            credentialAddress = w2.target.toULong(),
            credentialValue = w2.value.toULong(),
            seccompAddress = w3.target.toULong(),
            writeOffset = 0uL,
        )
    }
}
