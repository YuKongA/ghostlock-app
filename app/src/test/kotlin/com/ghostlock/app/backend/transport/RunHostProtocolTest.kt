package com.ghostlock.app.backend.transport

import com.ghostlock.app.domain.run.LogDraft
import com.ghostlock.app.domain.run.LogLevel
import com.ghostlock.app.domain.run.LogOrigin
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
        assertTrue(RunHostProtocol.isBackendKey("DIAGNOSE"))
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
            LogDraft(
                origin = LogOrigin.Kotlin,
                level = LogLevel.Info,
                message = "probe:\u001B[32mPASS\u001B[0m",
            ).isValidRunHostLogDraft(),
        )
        assertFalse(
            LogDraft(
                origin = LogOrigin.Native,
                level = LogLevel.Warn,
                message = "line-one\nline-two",
            ).isValidRunHostLogDraft(),
        )
        assertFalse(
            LogDraft(
                origin = LogOrigin.Shell,
                level = LogLevel.Error,
                message = "unsupported:\u001B[1mstyle",
            ).isValidRunHostLogDraft(),
        )
        assertFalse(isValidLogMessage("诊断"))
    }

    /** Stable log wire values remain independent of enum ordinal. */
    @Test
    fun logWireValuesRemainPinned() {
        assertEquals(1, LogLevel.Verbose.wireValue)
        assertEquals(5, LogLevel.Error.wireValue)
        assertEquals(1, LogOrigin.Kotlin.wireValue)
        assertEquals(3, LogOrigin.Shell.wireValue)
    }

    /** Source sequence has no cumulative count cap and fails only at Long exhaustion. */
    @Test
    fun logSequenceSupportsVolumeAndRejectsOverflow() {
        val counter = AtomicLong(0L)
        repeat(16_384) { index ->
            assertEquals(index.toLong() + 1L, reserveRunHostLogSequence(counter))
        }
        assertEquals(16_385L, reserveRunHostLogSequence(counter))
        counter.set(Long.MAX_VALUE - 1L)
        assertEquals(Long.MAX_VALUE, reserveRunHostLogSequence(counter))
        assertNull(reserveRunHostLogSequence(counter))
    }
}
