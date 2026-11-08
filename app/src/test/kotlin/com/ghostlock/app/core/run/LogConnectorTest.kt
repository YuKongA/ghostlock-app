package com.ghostlock.app.core.run

import com.ghostlock.app.data.log.FdLogConnector
import com.ghostlock.app.core.runhost.RunHostCode
import com.ghostlock.app.core.runhost.RunHostIdentity
import com.ghostlock.app.data.runhost.client.RunHostLogConnector
import com.ghostlock.app.core.runhost.RunHostSnapshotModel
import com.ghostlock.app.core.runhost.RunHostState
import com.ghostlock.app.domain.run.DiagnosticOutcome
import com.ghostlock.app.domain.run.LogConnectorEnd
import com.ghostlock.app.domain.run.LogDraft
import com.ghostlock.app.domain.run.LogLevel
import com.ghostlock.app.domain.run.LogOrigin
import com.ghostlock.app.domain.run.RunCapability
import com.ghostlock.app.domain.run.RunTerminal
import com.ghostlock.app.domain.run.StageResult
import com.ghostlock.app.run.RunHostLogRecord
import java.io.ByteArrayInputStream
import java.io.PipedInputStream
import java.io.PipedOutputStream
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.async
import kotlinx.coroutines.cancelAndJoin
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** Source-contract tests for complete FIFO drain, accepted-prefix failure, and terminal ownership. */
class LogConnectorTest {
    /** A Kotlin producer has no cumulative count gate and drains every accepted draft before normal completion. */
    @Test
    fun kotlinSourceDrainsUnlimitedFifoBeforeDrained() = runBlocking {
        val connector = KotlinLogConnector()
        val records = mutableListOf<LogDraft>()
        val collection = async(start = CoroutineStart.UNDISPATCHED) {
            connector.collect(records::add)
        }

        repeat(20_000) { index ->
            connector.emit(LogDraft(LogOrigin.Kotlin, LogLevel.Debug, "kotlin-$index"))
        }
        connector.finishInput()

        assertEquals(LogConnectorEnd.Drained, collection.await())
        assertEquals(20_000, records.size)
        assertEquals("kotlin-0", records.first().message)
        assertEquals("kotlin-19999", records.last().message)
    }

    /** Producer failure closes only the write side and preserves the complete already-accepted prefix. */
    @Test
    fun kotlinSourceFailureDrainsAcceptedPrefixAsIncomplete() = runBlocking {
        val connector = KotlinLogConnector()
        val records = mutableListOf<LogDraft>()
        val collection = async(start = CoroutineStart.UNDISPATCHED) {
            connector.collect(records::add)
        }

        connector.emit(LogDraft(LogOrigin.Kotlin, LogLevel.Info, "accepted-one"))
        connector.emit(LogDraft(LogOrigin.Kotlin, LogLevel.Info, "accepted-two"))
        connector.finishInputIncomplete("producer-failed")

        assertEquals(LogConnectorEnd.Incomplete("producer-failed"), collection.await())
        assertEquals(listOf("accepted-one", "accepted-two"), records.map(LogDraft::message))
    }

    /** FD parsing is chunk-independent, retains physical FIFO, and never reorders decreasing source clocks. */
    @Test
    fun fdSourceStreamsChunkedRecordsThroughEofWithoutTimestampSorting() = runBlocking {
        val bytes = "[I/200ms] native-first\n[W/100ms] native-second\n".toByteArray(Charsets.US_ASCII)
        val input = PipedInputStream()
        val output = PipedOutputStream(input)
        val connector = FdLogConnector(input, LogOrigin.Native, true)
        val records = mutableListOf<LogDraft>()
        val collection = async(Dispatchers.IO) { connector.collect(records::add) }

        bytes.asList().chunked(3).forEach { chunk ->
            output.write(chunk.toByteArray())
            output.flush()
        }
        assertFalse(collection.isCompleted)
        output.close()

        assertEquals(LogConnectorEnd.Drained, collection.await())
        connector.close()

        assertEquals(listOf("native-first", "native-second"), records.map(LogDraft::message))
        assertEquals(listOf(200L, 100L), records.map(LogDraft::sourceElapsedMs))
        assertEquals(listOf(LogLevel.Info, LogLevel.Warn), records.map(LogDraft::level))
    }

    /** Malformed, partial, and oversized FD tails stop acceptance while keeping each valid prefix. */
    @Test
    fun fdSourceReturnsIncompleteForEveryUnprovableTail() = runBlocking {
        val cases = listOf(
            "[I/1ms] accepted\nbroken\n[I/2ms] ignored\n" to listOf("accepted"),
            "[I/1ms] partial" to emptyList(),
            "[I/1ms] " + "x".repeat(9_000) + "\n" to emptyList(),
        )

        cases.forEach { (text, expected) ->
            val connector = FdLogConnector(
                ByteArrayInputStream(text.toByteArray(Charsets.US_ASCII)),
                LogOrigin.Native,
                true,
            )
            val records = mutableListOf<LogDraft>()
            val end = connector.collect(records::add)
            connector.close()
            assertTrue(end is LogConnectorEnd.Incomplete)
            assertEquals(expected, records.map(LogDraft::message))
        }
    }

    /** RunHost uses its source-private sequence for delivery only and preserves decreasing source timestamps. */
    @Test
    fun runHostSourceDrainsContiguousPrefixThroughTerminalMarker() = runBlocking {
        val connector = connector()
        val records = mutableListOf<LogDraft>()
        val collection = async(start = CoroutineStart.UNDISPATCHED) {
            connector.collect(records::add)
        }

        connector.receive(record(1L, 200L, "runhost-first"))
        connector.receive(record(2L, 100L, "runhost-second"))
        connector.receiveTerminal(snapshot(finalSequence = 2L))

        withTimeout(1_000L) { connector.terminalMarker.await() }
        assertEquals(LogConnectorEnd.Drained, collection.await())
        assertEquals(LogConnectorEnd.Drained, connector.end.await())
        assertEquals(listOf("runhost-first", "runhost-second"), records.map(LogDraft::message))
        assertEquals(listOf(200L, 100L), records.map(LogDraft::sourceElapsedMs))
    }

    /** Gap, duplicate, inversion, explicit close, and collector cancellation each yield one incomplete end. */
    @Test
    fun runHostSourceIntegrityFailuresKeepOnlyTheAcceptedPrefix() = runBlocking {
        val sequenceCases = listOf(
            listOf(1L, 3L) to listOf("sequence-1"),
            listOf(1L, 1L) to listOf("sequence-1"),
            listOf(1L, 2L, 1L) to listOf("sequence-1", "sequence-2"),
        )
        sequenceCases.forEach { (sequences, expected) ->
            val connector = connector()
            val records = mutableListOf<LogDraft>()
            val collection = async(start = CoroutineStart.UNDISPATCHED) {
                connector.collect(records::add)
            }
            sequences.forEach { sequence -> connector.receive(record(sequence, sequence, "sequence-$sequence")) }
            connector.receiveTerminal(snapshot(finalSequence = sequences.max()))
            assertTrue(collection.await() is LogConnectorEnd.Incomplete)
            assertEquals(expected, records.map(LogDraft::message))
        }

        val closed = connector()
        val closedRecords = mutableListOf<LogDraft>()
        val closedCollection = async(start = CoroutineStart.UNDISPATCHED) {
            closed.collect(closedRecords::add)
        }
        closed.receive(record(1L, 1L, "accepted-before-close"))
        closed.close()
        assertTrue(closedCollection.await() is LogConnectorEnd.Incomplete)
        assertEquals(listOf("accepted-before-close"), closedRecords.map(LogDraft::message))

        val cancelled = connector()
        val cancelledCollection = async(start = CoroutineStart.UNDISPATCHED) {
            cancelled.collect { }
        }
        cancelledCollection.cancelAndJoin()
        assertTrue(withTimeout(1_000L) { cancelled.end.await() } is LogConnectorEnd.Incomplete)
        cancelled.close()
    }

    /** @return One connector whose parcel measurement is deterministic on the host JVM. */
    private fun connector(): RunHostLogConnector = RunHostLogConnector(IDENTITY) { 0 }

    /** @return One exact source-private callback record. */
    private fun record(sequence: Long, sourceElapsedMs: Long, message: String): RunHostLogRecord =
        RunHostLogRecord().also { record ->
            record.runId = IDENTITY.runId
            record.generation = IDENTITY.generation
            record.sequence = sequence
            record.elapsedRealtimeMs = sourceElapsedMs
            record.origin = LogOrigin.Native.wireValue
            record.level = LogLevel.Info.wireValue
            record.message = message
        }

    /** @return One valid diagnostic terminal marker carrying the source's final sequence. */
    private fun snapshot(finalSequence: Long): RunHostSnapshotModel = RunHostSnapshotModel(
        identity = IDENTITY,
        state = RunHostState.TERMINAL,
        lastCode = RunHostCode.OK,
        runCapability = RunCapability.DIAGNOSTIC_ONLY,
        diagnosticOutcome = DiagnosticOutcome.PASS,
        backendPrimitive = StageResult.SUCCESS,
        scriptDispatch = StageResult.SKIPPED,
        payloadItems = emptyList(),
        payloadOverall = StageResult.SKIPPED,
        finishItems = emptyList(),
        finishOverall = StageResult.SKIPPED,
        backendCleanup = StageResult.SUCCESS,
        terminal = RunTerminal.DIAGNOSTIC_COMPLETE,
        logSequence = finalSequence,
        droppedLogs = 0L,
    )

    private companion object {
        val IDENTITY: RunHostIdentity = RunHostIdentity("0".repeat(32), 1L)
    }
}
