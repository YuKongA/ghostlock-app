package com.ghostlock.app.data.runhost.client

import com.ghostlock.app.core.runhost.RunHostIdentity
import com.ghostlock.app.core.runhost.isValidRunHostLogDraft
import com.ghostlock.app.core.runhost.isValidRunHostLogMessage
import com.ghostlock.app.core.runhost.reserveRunHostLogSequence
import com.ghostlock.app.domain.run.LogDraft
import com.ghostlock.app.domain.run.LogLevel
import com.ghostlock.app.domain.run.LogOrigin
import com.ghostlock.app.run.RunHostLogRecord
import java.util.concurrent.atomic.AtomicLong
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** Source-local log validation and sequence cases for the RunHost callback adapter. */
class RunHostLogCursorTest {
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
        assertFalse(isValidRunHostLogMessage("诊断"))
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

    /** Callback sequence validates one RunHost source and leaves source time as unordered metadata. */
    @Test
    fun cursorKeepsSourceSequencePrivateToDeliveryValidation() {
        val identity = RunHostIdentity("0".repeat(32), 1L)
        val cursor = RunHostLogCursor(identity) { 0 }
        val first = cursor.accept(logRecord(identity, 1L, 900L))
        assertTrue(first is RunHostLogCursorResult.Accepted)
        first as RunHostLogCursorResult.Accepted
        assertEquals(900L, first.draft.sourceElapsedMs)
        assertTrue(cursor.accept(logRecord(identity, 1L, 100L)) is RunHostLogCursorResult.Rejected)
        assertEquals("runhost-log-terminal-gap", cursor.terminalFailure(2L, 0L))
    }

    /** @return One bounded typed callback record for cursor-only host tests. */
    private fun logRecord(
        identity: RunHostIdentity,
        sequence: Long,
        elapsedMs: Long,
    ): RunHostLogRecord = RunHostLogRecord().also { record ->
        record.runId = identity.runId
        record.generation = identity.generation
        record.sequence = sequence
        record.elapsedRealtimeMs = elapsedMs
        record.origin = LogOrigin.Native.wireValue
        record.level = LogLevel.Info.wireValue
        record.message = "cursor-$sequence"
    }
}
