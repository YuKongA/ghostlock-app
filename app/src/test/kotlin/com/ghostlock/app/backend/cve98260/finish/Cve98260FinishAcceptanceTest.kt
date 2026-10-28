package com.ghostlock.app.backend.cve98260.finish

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * R1d（收敛后）单测：finish 证据合同（UI 生成脚本，backend 只验收 service 事实）。
 */
class Cve98260FinishAcceptanceTest {
    private fun valid(
        requireSelinux: Boolean = false,
        selinux: Boolean? = true,
    ): Cve98260FinishEvidence = Cve98260FinishEvidence(
        runId = "run-12345678",
        scriptSha256 = "abc",
        expectedScriptSha256 = "abc",
        scriptPresent = true,
        exitCode = 0,
        timedOut = false,
        observedUid = 0L,
        selinuxEnforcing = selinux,
        ksudPresent = true,
        requireSelinuxEnforcing = requireSelinux,
    )

    @Test
    fun acceptsCompleteEvidence() {
        assertEquals(
            Cve98260FinishAcceptance.Verdict.Accepted,
            Cve98260FinishAcceptance.evaluate(valid()),
        )
        assertEquals(
            Cve98260FinishAcceptance.Verdict.Accepted,
            Cve98260FinishAcceptance.evaluate(valid(requireSelinux = true)),
        )
    }

    @Test
    fun selinuxRequiredOnlyWhenUiRequestsRestore() {
        assertEquals(
            Cve98260FinishAcceptance.Verdict.Accepted,
            Cve98260FinishAcceptance.evaluate(valid(requireSelinux = false, selinux = false)),
        )
        val refused = Cve98260FinishAcceptance.evaluate(valid(requireSelinux = true, selinux = false))
        assertTrue(refused is Cve98260FinishAcceptance.Verdict.Refused)
        assertTrue((refused as Cve98260FinishAcceptance.Verdict.Refused).reasons.contains("cve98260-finish:selinux"))
    }

    @Test
    fun refusesEachMissingFact() {
        val cases = listOf(
            valid().copy(runId = "bad") to "cve98260-finish:run-id",
            valid().copy(scriptPresent = false) to "cve98260-finish:script-missing",
            valid().copy(scriptSha256 = "other") to "cve98260-finish:script-sha-mismatch",
            valid().copy(exitCode = 1) to "cve98260-finish:exit:1",
            valid().copy(exitCode = null) to "cve98260-finish:exit:missing",
            valid().copy(timedOut = true) to "cve98260-finish:timeout",
            valid().copy(observedUid = 2000L) to "cve98260-finish:uid:2000",
            valid().copy(observedUid = null) to "cve98260-finish:uid:missing",
            valid().copy(ksudPresent = false) to "cve98260-finish:ksud-missing",
        )
        for ((evidence, reason) in cases) {
            val verdict = Cve98260FinishAcceptance.evaluate(evidence)
            assertTrue(verdict is Cve98260FinishAcceptance.Verdict.Refused)
            assertTrue((verdict as Cve98260FinishAcceptance.Verdict.Refused).reasons.contains(reason))
        }
    }
}
