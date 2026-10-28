package com.ghostlock.app.backend.cve43499.finish

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * B6/task-82 finish enforce 语义单测（先写不运行）。
 *
 * 覆盖：acceptance 要求 enforce 恢复到**运行前记录值**；before 缺失/after≠before 均拒绝。
 */
class FinishEnforceRestoreTest {

    private fun reasons(before: Int?, after: Int?): List<String> {
        val verdict = FinishAcceptance.evaluate(
            FinishAcceptanceInput(
                runId = "run",
                observedUid = null,
                handoff = null,
                resourcesReleased = true,
                execution = null,
                binding = null,
                enforceBefore = before,
                enforceAfter = after,
            ),
        )
        return (verdict as FinishVerdict.Refused).reasons
    }

    @Test
    fun `enforce restored to recorded value is not a selinux refusal`() {
        assertTrue(reasons(1, 1).none { it.startsWith("finish-selinux") })
        assertTrue(reasons(0, 0).none { it.startsWith("finish-selinux") })
    }

    @Test
    fun `enforce mismatch or missing is named refusal`() {
        assertTrue(reasons(1, 0).any { it.startsWith("finish-selinux-not-restored:1:0") })
        assertTrue(reasons(null, 1).contains("finish-selinux-missing"))
        val missingAfter = reasons(1, null)
        assertTrue(missingAfter.contains("finish-selinux-missing"))
        assertFalse(missingAfter.any { it.startsWith("finish-selinux-not-restored") })
    }
}
