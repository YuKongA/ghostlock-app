package com.ghostlock.app.core.run

import com.ghostlock.app.domain.run.DiagnosticOutcome
import com.ghostlock.app.domain.run.RunCapability
import com.ghostlock.app.domain.run.RunExecutionResult
import com.ghostlock.app.domain.run.RunLogDocumentFormatter
import com.ghostlock.app.domain.run.RunLogFormatConfig
import com.ghostlock.app.domain.run.RunLogLevel
import com.ghostlock.app.domain.run.RunLogOrigin
import com.ghostlock.app.domain.run.RunLogRecord
import com.ghostlock.app.domain.run.RunLogSink
import com.ghostlock.app.domain.run.RunLogSinkEmitResult
import com.ghostlock.app.domain.run.RunLogSinkFactory
import com.ghostlock.app.domain.run.RunLogSinkFailure
import com.ghostlock.app.domain.run.RunLogSinkFailureKind
import com.ghostlock.app.domain.run.RunLogSinkRequest
import com.ghostlock.app.domain.run.RunReport
import com.ghostlock.app.domain.run.RunSessionPhase
import com.ghostlock.app.domain.run.RunSessionState
import com.ghostlock.app.domain.run.RunTerminal
import com.ghostlock.app.domain.run.StageResult
import java.io.ByteArrayOutputStream
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicInteger
import kotlin.concurrent.thread
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import kotlinx.coroutines.yield
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

/** Locks direct sink ownership, canonical/manual parity, and non-authoritative first-failure callbacks. */
@RunWith(RobolectricTestRunner::class)
class RunCoordinatorLogSinkTest {
    /** A rejected start leaves ownership free; the next start is accepted and a concurrent start is rejected. */
    @Test
    fun rejectedThenAcceptedRunPreservesSingleOwner() = runBlocking {
        val factory = RecordingFactory()
        val coordinator = RunCoordinator(factory, RunLogDocumentFormatter())

        val rejected = coordinator.start(
            context = context(startAllowed = false),
            sinkRequest = request(enabled = true),
        ) { RunExecutionResult.Completed(report()) }

        assertFalse(rejected)
        assertEquals(0, factory.createCount.get())
        assertTrue(factory.sinks().isEmpty())

        val backendEntered = CountDownLatch(1)
        val releaseBackend = CountDownLatch(1)
        assertTrue(
            coordinator.start(context(), request(enabled = true)) {
                backendEntered.countDown()
                assertTrue(releaseBackend.await(2L, TimeUnit.SECONDS))
                RunExecutionResult.Completed(report())
            },
        )
        assertTrue(backendEntered.await(2L, TimeUnit.SECONDS))
        val canonicalBeforeConcurrentReject = coordinator.renderCanonicalDocument()
        assertFalse(
            coordinator.start(context(), request(enabled = true, generation = 2L)) {
                RunExecutionResult.Completed(report())
            },
        )
        assertEquals(1, factory.createCount.get())
        assertArrayEquals(canonicalBeforeConcurrentReject, coordinator.renderCanonicalDocument())

        releaseBackend.countDown()
        waitForReleased(coordinator, RunSessionPhase.Done)
        assertEquals(1, factory.sinks().single().finishCount.get())
        assertTrue(coordinator.state.value.report?.isProductSuccess == true)
    }

    /** One accepted terminal finishes once and matches the manual canonical document. */
    @Test
    fun acceptedRunFinishesOnceAndMatchesManualBytes() = runBlocking {
        val factory = RecordingFactory()
        val coordinator = RunCoordinator(factory, RunLogDocumentFormatter())

        assertTrue(
            coordinator.start(context(), request(enabled = true)) { emit ->
                emit(event(sequence = 1L, body = "event-one"))
                RunExecutionResult.Completed(report())
            },
        )
        withTimeout(5_000L) { coordinator.state.first { value -> value.phase == RunSessionPhase.Done } }
        val sink = factory.sinks().single()
        withTimeout(5_000L) {
            while (sink.finishCount.get() != 1) yield()
        }

        assertEquals(1, factory.createCount.get())
        assertArrayEquals(coordinator.renderCanonicalDocument(), sink.bytes())
        assertTrue(coordinator.state.value.report?.isProductSuccess == true)

        assertTerminalProjection(
            result = RunExecutionResult.Refused(listOf("typed refusal")),
            expectedPhase = RunSessionPhase.Done,
            reportExpected = false,
        )
        assertTerminalProjection(
            result = RunExecutionResult.TransportFailure("typed transport failure"),
            expectedPhase = RunSessionPhase.Done,
            reportExpected = false,
        )
        assertTerminalProjection(
            result = RunExecutionResult.Completed(report(RunTerminal.REBOOT_REQUESTED)),
            expectedPhase = RunSessionPhase.HandoffRequested,
            reportExpected = true,
        )
    }

    /** Abort before a non-suspending backend return still publishes one terminal and releases the active slot. */
    @Test
    fun abortImmediatelyBeforeBackendReturnStillFinalizesOnce() = runBlocking {
        val factory = RecordingFactory()
        val coordinator = RunCoordinator(factory, RunLogDocumentFormatter())

        assertTrue(
            coordinator.start(context(), request(enabled = true)) {
                assertTrue(coordinator.requestAbort("unused disabled message"))
                RunExecutionResult.Completed(report())
            },
        )
        withTimeout(5_000L) {
            coordinator.state.first { value -> value.phase == RunSessionPhase.Aborted }
        }
        withTimeout(5_000L) {
            while (coordinator.isRunning()) yield()
        }

        val sink = factory.sinks().single()
        assertEquals(1, sink.finishCount.get())
        assertFalse(coordinator.isRunning())
        assertEquals(null, coordinator.state.value.report)
        assertArrayEquals(coordinator.renderCanonicalDocument(), sink.bytes())
        val text = sink.bytes().toString(Charsets.UTF_8)
        assertTrue(text.contains("session\tphase=Aborted\n"))
        assertTrue(text.contains("report\tpresent=false\n"))
    }

    /** Presentation clear never mutates the retained canonical document. */
    @Test
    fun clearTerminalPreservesCanonicalBytes() = runBlocking {
        val coordinator = RunCoordinator(RecordingFactory(), RunLogDocumentFormatter())
        val runningBeforeClear = java.util.concurrent.atomic.AtomicReference<ByteArray>()
        val runningAfterClear = java.util.concurrent.atomic.AtomicReference<ByteArray>()
        val runningPresentationCleared = AtomicBoolean(false)
        assertTrue(
            coordinator.start(context(), request(enabled = false)) { emit ->
                emit(event(sequence = 1L, body = "retained canonical row"))
                runningBeforeClear.set(coordinator.renderCanonicalDocument())
                coordinator.clearTerminal()
                runningPresentationCleared.set(coordinator.state.value.entries.isEmpty())
                runningAfterClear.set(coordinator.renderCanonicalDocument())
                RunExecutionResult.Completed(report())
            },
        )
        withTimeout(5_000L) { coordinator.state.first { value -> value.phase == RunSessionPhase.Done } }
        assertTrue(runningPresentationCleared.get())
        assertArrayEquals(runningBeforeClear.get(), runningAfterClear.get())
        val terminalBeforeClear = coordinator.renderCanonicalDocument()

        coordinator.clearTerminal()

        assertTrue(coordinator.state.value.entries.isEmpty())
        assertArrayEquals(terminalBeforeClear, coordinator.renderCanonicalDocument())
    }

    /** The canonical lock prevents a later producer from enqueueing before an earlier committed row. */
    @Test
    fun concurrentProducerInterleavingPreservesCommitOrder() = runBlocking {
        val sink = InterleavingSink()
        val coordinator = RunCoordinator(RecordingFactory { sink }, RunLogDocumentFormatter())
        assertTrue(
            coordinator.start(context(), request(enabled = true)) { emit ->
                val first = thread(start = true) { emit(event(1L, "A")) }
                assertTrue(sink.firstEntryInsideEmit.await(2L, TimeUnit.SECONDS))
                val secondStarted = CountDownLatch(1)
                val second = thread(start = true) {
                    secondStarted.countDown()
                    emit(event(2L, "B"))
                }
                assertTrue(secondStarted.await(2L, TimeUnit.SECONDS))
                sink.releaseFirstEntry.countDown()
                first.join(2_000L)
                second.join(2_000L)
                RunExecutionResult.Completed(report())
            },
        )
        withTimeout(5_000L) { coordinator.state.first { value -> value.phase == RunSessionPhase.Done } }

        val text = sink.bytes().toString(Charsets.UTF_8)
        assertTrue(text.indexOf("message=A") < text.indexOf("message=B"))
        assertArrayEquals(coordinator.renderCanonicalDocument(), sink.bytes())
    }

    /** One frozen threshold filters before every sink while retaining origin, level, and commit order. */
    @Test
    fun thresholdAndTypedOriginsAreSharedByEverySink() = runBlocking {
        val factory = RecordingFactory()
        val coordinator = RunCoordinator(factory, RunLogDocumentFormatter())
        assertTrue(
            coordinator.start(
                context(minimumLogLevel = RunLogLevel.Info),
                request(enabled = true),
            ) { emit ->
                emit(record(RunLogOrigin.Native, RunLogLevel.Debug, "native-filtered"))
                emit(record(RunLogOrigin.Shell, RunLogLevel.Info, "shell-info"))
                emit(record(RunLogOrigin.Kotlin, RunLogLevel.Warn, "kotlin-warn"))
                RunExecutionResult.Completed(report())
            },
        )
        waitForIdle(coordinator)

        val entries = coordinator.state.value.entries
        assertFalse(entries.any { entry -> entry.message == "native-filtered" })
        assertTrue(entries.any { entry ->
            entry.origin == RunLogOrigin.Shell &&
                entry.level == RunLogLevel.Info &&
                entry.message == "shell-info"
        })
        assertTrue(entries.indexOfFirst { entry -> entry.message == "shell-info" } <
            entries.indexOfFirst { entry -> entry.message == "kotlin-warn" })
        assertArrayEquals(coordinator.renderCanonicalDocument(), factory.sinks().single().bytes())
    }

    /** Failure callbacks are warning-only, deduplicated, and isolated from the next run token. */
    @Test
    fun firstFailureCallbackIsPresentationOnlyAndTokenIsolated() = runBlocking {
        val factory = RecordingFactory()
        val coordinator = RunCoordinator(factory, RunLogDocumentFormatter())
        assertTrue(
            coordinator.start(context(), request(enabled = true, generation = 1L)) {
                RunExecutionResult.Completed(report())
            },
        )
        waitForIdle(coordinator)
        assertTrue(
            coordinator.start(context(), request(enabled = true, generation = 2L)) {
                RunExecutionResult.Completed(report())
            },
        )
        waitForIdle(coordinator)

        factory.publishFailure(0)
        assertEquals(0, warningCount(coordinator))
        factory.publishFailure(1)
        factory.publishFailure(1)

        val state = coordinator.state.value
        assertNotNull(state.report)
        assertTrue(state.report?.isProductSuccess == true)
        assertEquals(1, warningCount(coordinator))
        assertFalse(coordinator.renderCanonicalDocument().toString(Charsets.UTF_8).contains("automatic export:"))
    }

    /** Waits until the terminal is visible and the active slot has been released. */
    private suspend fun waitForIdle(coordinator: RunCoordinator) {
        waitForReleased(coordinator, RunSessionPhase.Done)
    }

    /** Waits for [expectedPhase] and the release of the sole active slot. */
    private suspend fun waitForReleased(
        coordinator: RunCoordinator,
        expectedPhase: RunSessionPhase,
    ) {
        withTimeout(5_000L) { coordinator.state.first { value -> value.phase == expectedPhase } }
        withTimeout(5_000L) {
            while (coordinator.isRunning()) yield()
        }
    }

    /** Proves one non-success or handoff result preserves its typed phase/report and finishes once. */
    private suspend fun assertTerminalProjection(
        result: RunExecutionResult,
        expectedPhase: RunSessionPhase,
        reportExpected: Boolean,
    ) {
        val factory = RecordingFactory()
        val coordinator = RunCoordinator(factory, RunLogDocumentFormatter())
        assertTrue(
            coordinator.start(context(), request(enabled = true)) { result },
        )
        waitForReleased(coordinator, expectedPhase)

        assertEquals(reportExpected, coordinator.state.value.report != null)
        assertEquals(expectedPhase, coordinator.state.value.phase)
        assertEquals(1, factory.createCount.get())
        assertEquals(1, factory.sinks().single().finishCount.get())
        assertArrayEquals(coordinator.renderCanonicalDocument(), factory.sinks().single().bytes())
    }

    /** @return Number of presentation-only automatic-export warnings. */
    private fun warningCount(coordinator: RunCoordinator): Int =
        coordinator.state.value.entries.count { entry -> entry.message.startsWith("automatic export:") }

    /** @return Gate-approved application session projection. */
    private fun context(
        startAllowed: Boolean = true,
        minimumLogLevel: RunLogLevel = RunLogLevel.Info,
    ): RunSessionState = RunSessionState(
        backendKey = "backend",
        backendLabel = "Backend",
        startAllowed = startAllowed,
        abortable = true,
        minimumLogLevel = minimumLogLevel,
    )

    /** @return Frozen automatic-export request. */
    private fun request(enabled: Boolean, generation: Long = 1L): RunLogSinkRequest = RunLogSinkRequest(
        enabled = enabled,
        runId = "0123456789abcdef0123456789abcdef",
        generation = generation,
        backendKey = "backend",
        relativePath = "Download/ghostlock-log",
        formatConfig = RunLogFormatConfig(showTimestamp = false),
    )

    /** @return One typed canonical record; coordinator commit order remains authoritative. */
    private fun event(sequence: Long, body: String): RunLogRecord = RunLogRecord(
        origin = RunLogOrigin.Kotlin,
        level = RunLogLevel.Info,
        message = body,
        elapsedMs = sequence,
    )

    /** @return One typed source record before coordinator elapsed-time stamping. */
    private fun record(origin: RunLogOrigin, level: RunLogLevel, body: String): RunLogRecord = RunLogRecord(
        origin = origin,
        level = level,
        message = body,
    )

    /** @return Complete product success report whose authority is independent from export. */
    private fun report(terminal: RunTerminal = RunTerminal.SUCCESS): RunReport = RunReport(
        runCapability = RunCapability.PRODUCT_ROOT,
        diagnosticOutcome = DiagnosticOutcome.NOT_APPLICABLE,
        backendPrimitive = StageResult.SUCCESS,
        rootScriptDispatch = StageResult.SUCCESS,
        payloadItems = emptyList(),
        payloadOverall = StageResult.SUCCESS,
        finishItems = emptyList(),
        finishOverall = StageResult.SUCCESS,
        backendCleanup = StageResult.SUCCESS,
        terminal = terminal,
    )

    /** Immediate fake factory that records each run-scoped callback and sink. */
    private class RecordingFactory(
        private val createSink: () -> RecordingSink = { RecordingSink() },
    ) : RunLogSinkFactory {
        val createCount = AtomicInteger(0)
        private val lock = Any()
        private val createdSinks = mutableListOf<RecordingSink>()
        private val callbacks = mutableListOf<(RunLogSinkFailure) -> Unit>()

        override fun create(
            request: RunLogSinkRequest,
            onFailure: (RunLogSinkFailure) -> Unit,
        ): RunLogSink = synchronized(lock) {
            createCount.incrementAndGet()
            callbacks += onFailure
            createSink().also(createdSinks::add)
        }

        fun sinks(): List<RecordingSink> = synchronized(lock) { createdSinks.toList() }

        fun publishFailure(index: Int) {
            val callback = synchronized(lock) { callbacks[index] }
            callback(
                RunLogSinkFailure(
                    RunLogSinkFailureKind.WRITE,
                    "forced test failure",
                ),
            )
        }
    }

    /** Thread-safe in-memory sink whose producer method performs no platform I/O. */
    private open class RecordingSink : RunLogSink {
        private val lock = Any()
        private val output = ByteArrayOutputStream()
        private val accepting = AtomicBoolean(true)
        val finishCount = AtomicInteger(0)

        override fun tryEmit(rows: List<ByteArray>): RunLogSinkEmitResult {
            if (!accepting.get()) return RunLogSinkEmitResult.REJECTED
            synchronized(lock) { rows.forEach { row -> output.write(row) } }
            return RunLogSinkEmitResult.ACCEPTED
        }

        override fun finish() {
            if (accepting.compareAndSet(true, false)) finishCount.incrementAndGet()
        }

        fun bytes(): ByteArray = synchronized(lock) { output.toByteArray() }
    }

    /** Pauses the first entry enqueue while a later producer waits on the coordinator lock. */
    private class InterleavingSink : RecordingSink() {
        val firstEntryInsideEmit = CountDownLatch(1)
        val releaseFirstEntry = CountDownLatch(1)
        private val emits = AtomicInteger(0)

        override fun tryEmit(rows: List<ByteArray>): RunLogSinkEmitResult {
            if (emits.incrementAndGet() == 2) {
                firstEntryInsideEmit.countDown()
                assertTrue(releaseFirstEntry.await(2L, TimeUnit.SECONDS))
            }
            return super.tryEmit(rows)
        }
    }
}
