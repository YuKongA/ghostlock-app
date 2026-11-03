package com.ghostlock.app.backend.backends.cve43499.w3

import com.ghostlock.app.backend.backends.cve43499.victim.VictimProtocolAppend
import com.ghostlock.app.core.victim.VictimCapability
import com.ghostlock.app.core.victim.VictimCapabilities
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * G04 W3 硬门单测（task-78；先写不运行）。
 *
 * 覆盖：I1 capability bits 7/8/9；I3 status/comm 行协议固定格式/4 KiB 上限/hex 小写；
 * P1–P8 前置/final 矩阵（uid/cred/seccomp 双源一致、mismatch/missing FAIL、leaf 0/8/15/歧义、
 * close-retained、finish 前置、coldRebootRequired evidence）。
 */
class Cve43499W3HardGateTest {

    private fun status(uid: String = "0\t0\t0\t0", capEff: String = "000001ffffffffff", seccomp: Int = 0): String =
        "Name:\tglk\nUid:\t$uid\nCapEff:\t$capEff\nSeccomp:\t$seccomp\n"

    private fun gate(): Cve43499W3HardGate = Cve43499W3HardGate(
        parentStatusReader = { status() },
        bootIdReader = { "boot-1" },
        ksuLoaded = { false },
    )

    private fun preInput(
        self: ByteArray? = status().toByteArray(),
        seccompBefore: Int? = 2,
        finishOk: Boolean = true,
        uid: Long? = 0L,
    ): Cve43499W3HardGate.PreInput = Cve43499W3HardGate.PreInput(
        victimPid = 4242,
        startTimeBefore = 777L,
        startTimeAfter = 777L,
        victimUid = uid,
        selfStatus = self,
        seccompBefore = seccompBefore,
        bootId = "boot-1",
        ksuGuard = "absent",
        finishPrereqOk = finishOk,
    )

    @Test
    fun `capability bits append 7 8 9 without reorder`() {
        assertEquals(1 shl 0, VictimCapability.PROBE_UID.bit)
        assertEquals(1 shl 6, VictimCapability.DETACH_ROOTED.bit)
        assertEquals(1 shl 7, VictimCapability.PROBE_STATUS.bit)
        assertEquals(1 shl 8, VictimCapability.PROBE_COMM_SET.bit)
        assertEquals(1 shl 9, VictimCapability.PROBE_COMM.bit)
        val all = VictimCapabilities.of(*VictimCapability.entries.toTypedArray())
        assertTrue(all.contains(VictimCapability.PROBE_STATUS))
        assertTrue(all.contains(VictimCapability.PROBE_COMM_SET))
        assertTrue(all.contains(VictimCapability.PROBE_COMM))
    }

    @Test
    fun `status and comm line protocol frozen formats`() {
        val raw = status().toByteArray()
        val hex = raw.joinToString("") { String.format("%02x", it.toInt() and 0xFF) }
        val parsed = VictimProtocolAppend.parseStatusLine("GLK_STATUS:$hex")
        assertEquals(raw.size, parsed!!.size)
        assertNull(VictimProtocolAppend.parseStatusLine("GLK_STATUS:ABCD"))
        assertEquals(0, VictimProtocolAppend.parseCommLine("GLK_COMM:0:")!!.first)
        val comm = VictimProtocolAppend.parseCommLine("GLK_COMM:2:6f6b")
        assertEquals(2, comm!!.first)
        assertEquals("ok", String(comm.second, Charsets.US_ASCII))
        assertNull(VictimProtocolAppend.parseCommLine("GLK_COMM:3:6f6b"))
        assertNull(VictimProtocolAppend.parseCommLine("GLK_COMM:16:" + "00".repeat(16)))
        assertTrue(VictimProtocolAppend.parseCommSetLine("GLK_COMM_SET:OK"))
        assertFalse(VictimProtocolAppend.parseCommSetLine("GLK_COMM_SET:ERR:io"))
        assertNull(VictimProtocolAppend.commSetCommand("contains space"))
        assertNull(VictimProtocolAppend.commSetCommand("x".repeat(16)))
        assertEquals("comm-set marker", VictimProtocolAppend.commSetCommand("marker"))
        assertNull(VictimProtocolAppend.parseStatusLine("GLK_STATUS:" + "ab".repeat(4097)))
    }

    @Test
    fun `pre gate passes on consistent dual source`() {
        val verdict = gate().evaluatePre(preInput())
        assertTrue(verdict.reason ?: "", verdict.passed)
        assertEquals("0/0/0/0", verdict.evidence["w3UidParent"])
        assertEquals("0", verdict.evidence["w3SeccompParent"])
    }

    @Test
    fun `pre gate fails closed on mismatch missing and not root`() {
        val mismatch = Cve43499W3HardGate(
            parentStatusReader = { status(uid = "0\t0\t0\t1000") },
            bootIdReader = { "boot-1" },
        ).evaluatePre(preInput())
        assertFalse(mismatch.passed)
        assertEquals("gate-uid-not-root", mismatch.reason)
        val missing = gate().evaluatePre(preInput(self = null))
        assertFalse(missing.passed)
        assertEquals("gate-seccomp-probe-missing", missing.reason)
        val seccompMissing = gate().evaluatePre(preInput(seccompBefore = null))
        assertFalse(seccompMissing.passed)
        assertEquals("gate-seccomp-not-cleared", seccompMissing.reason)
        val finish = gate().evaluatePre(preInput(finishOk = false))
        assertFalse(finish.passed)
        assertEquals("gate-finish-prereq", finish.reason)
    }

    @Test
    fun `final gate requires direction clean close and seccomp zero`() {
        val pre = gate().evaluatePre(preInput())
        val pass = gate().evaluateFinal(
            pre = pre,
            selfStatusAfter = status(seccomp = 0).toByteArray(),
            parentStatusAfter = status(seccomp = 0),
            seccompAfterParent = 0,
            leafDirection = false,
            exactPolicy = false,
            closeClean = true,
            coldRebootRequired = true,
        )
        assertTrue(pass.passed)
        val retained = gate().evaluateFinal(pre, status().toByteArray(), status(), 0, false, false, false, true)
        assertFalse(retained.passed)
        assertEquals("gate-close-retained", retained.reason)
        val ambiguous = gate().evaluateFinal(pre, status().toByteArray(), status(), 0, null, false, true, true)
        assertFalse(ambiguous.passed)
        assertEquals("gate-leaf-direction", ambiguous.reason)
        val notCleared = gate().evaluateFinal(pre, status(seccomp = 2).toByteArray(), status(seccomp = 2), 2, false, false, true, true)
        assertFalse(notCleared.passed)
        assertEquals("gate-seccomp-not-cleared", notCleared.reason)
    }

    @Test
    fun `leaf decision matrix`() {
        assertEquals(Cve43499W3HardGate.LeafDecision.Exact, Cve43499W3HardGate.leafDecisionFromComm(0))
        assertEquals(Cve43499W3HardGate.LeafDecision.TargetPlus8, Cve43499W3HardGate.leafDecisionFromComm(8))
        assertEquals(Cve43499W3HardGate.LeafDecision.MissRetry, Cve43499W3HardGate.leafDecisionFromComm(15))
        assertEquals(Cve43499W3HardGate.LeafDecision.Ambiguous, Cve43499W3HardGate.leafDecisionFromComm(3))
    }
}
