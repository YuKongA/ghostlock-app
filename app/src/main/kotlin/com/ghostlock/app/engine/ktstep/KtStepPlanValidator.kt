package com.ghostlock.app.engine.ktstep

import com.ghostlock.app.core.steps.StepId

/** KtStepPlan 的静态形状校验；执行前唯一入口。 */
object KtStepPlanValidator {
    fun validate(plan: KtStepPlan): String? {
        if (plan.pageSize <= 0) return "invalid:page-size"
        if (plan.attempts < 1) return "invalid:attempts"
        if (plan.settleMs < 0L) return "invalid:settle"
        if (plan.expectedUid < 0L) return "invalid:expected-uid"
        if (plan.layoutInputs.pointerBytes !in 1..8) return "invalid:pointer-bytes"
        if (plan.pageInputs.pointerBytes != plan.layoutInputs.pointerBytes) return "invalid:pointer-bytes-mismatch"
        val expected = setOf(StepId.W1, StepId.W2, StepId.W3)
        if (plan.steps.keys != expected) return "invalid:steps"
        val addresses = plan.steps.values.map { it.target }
        if (addresses.any { it == 0L }) return "invalid:target"
        if (addresses.distinct().size != addresses.size) return "invalid:target-duplicate"
        for ((id, step) in plan.steps) {
            if (step.step != id) return "invalid:step-id"
        }
        val w1 = plan.steps.getValue(StepId.W1)
        val w2 = plan.steps.getValue(StepId.W2)
        val w3 = plan.steps.getValue(StepId.W3)
        if (w1.mode != WriteMode.Zero || !w1.preserveChild || w1.value != 0L) return "invalid:step-shape:w1"
        if (w2.mode != WriteMode.Credential || !w2.preserveChild || w2.value != plan.layoutInputs.credentialValue) {
            return "invalid:step-shape:w2"
        }
        if (w3.mode != WriteMode.Zero || w3.preserveChild || w3.value != 0L) return "invalid:step-shape:w3"
        return null
    }
}
