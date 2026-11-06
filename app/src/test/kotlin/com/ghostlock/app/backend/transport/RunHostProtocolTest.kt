package com.ghostlock.app.backend.transport

import com.ghostlock.app.domain.run.RunLogDraft
import com.ghostlock.app.domain.run.RunLogLevel
import com.ghostlock.app.domain.run.RunLogOrigin
import java.util.concurrent.atomic.AtomicLong
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** Pure JVM contract cases for stable RunHost codes and bounded grammar. */
class RunHostProtocolTest {
    /** Stable integer values remain independent of enum ordinal. */
    @Test
    fun stableCodesRemainPinned() {
        assertEquals(0, RunHostCode.OK.wireValue)
        assertEquals(1, RunHostCode.ALREADY_APPLIED.wireValue)
        assertEquals(10, RunHostCode.BAD_STATE.wireValue)
        assertEquals(24, RunHostCode.EXECUTION_DOMAIN_MISMATCH.wireValue)
        assertEquals(RunHostCode.INTERNAL_CONTRACT_FAILURE, RunHostCode.fromWireValue(999))
    }

    /** Every wire identity grammar accepts only its canonical bounded form. */
    @Test
    fun wireGrammarIsStrict() {
        assertTrue(RunHostProtocol.isRunId("0".repeat(32)))
        assertFalse(RunHostProtocol.isRunId("0".repeat(31)))
        assertTrue(RunHostProtocol.isBackendKey("DEBUG-PROBE"))
        assertFalse(RunHostProtocol.isBackendKey("bad key"))
        assertTrue(RunHostProtocol.isLogicalId("profile-v3"))
        assertFalse(RunHostProtocol.isLogicalId("Profile"))
        assertTrue(RunHostProtocol.isSha256("a".repeat(64)))
        assertFalse(RunHostProtocol.isSha256("A".repeat(64)))
    }

    /** Log drafts accept printable ASCII and approved foreground/reset SGR only. */
    @Test
    fun logDraftGrammarIsStrict() {
        assertTrue(
            RunLogDraft(
                origin = RunLogOrigin.Kotlin,
                level = RunLogLevel.Info,
                message = "probe:\u001B[32mPASS\u001B[0m",
            ).isValidRunHostLogDraft(),
        )
        assertFalse(
            RunLogDraft(
                origin = RunLogOrigin.Native,
                level = RunLogLevel.Warn,
                message = "line-one\nline-two",
            ).isValidRunHostLogDraft(),
        )
        assertFalse(
            RunLogDraft(
                origin = RunLogOrigin.Shell,
                level = RunLogLevel.Error,
                message = "unsupported:\u001B[1mstyle",
            ).isValidRunHostLogDraft(),
        )
        assertFalse(isValidRunLogMessage("诊断"))
    }

    /** Stable log wire values remain independent of enum ordinal. */
    @Test
    fun logWireValuesRemainPinned() {
        assertEquals(1, RunLogLevel.Verbose.wireValue)
        assertEquals(5, RunLogLevel.Error.wireValue)
        assertEquals(1, RunLogOrigin.Kotlin.wireValue)
        assertEquals(3, RunLogOrigin.Shell.wireValue)
    }

    /** The log sequence authority stops at the fixed non-terminal callback budget. */
    @Test
    fun logSequenceBudgetIsHardBounded() {
        val counter = AtomicLong(0L)
        repeat(RunHostProtocol.MAX_QUEUED_LOGS) { index ->
            assertEquals(index.toLong() + 1L, reserveRunHostLogSequence(counter))
        }
        assertNull(reserveRunHostLogSequence(counter))
    }
}
