package com.ghostlock.app.core.run

import com.ghostlock.app.domain.run.*
import java.io.File
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicInteger
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.async
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** Pure stream-topology tests: one commit order, ordinary Info report rows, and independent sink backlog. */
class LogHubSinkTest {
    @Test
    fun suppliedSinkValidationAndFailureWarningDoNotStopFlow() = runBlocking {
        val hub = LogHub(LogDocumentFormatter())
        assertTrue(runCatching {
            hub.start(context(), request(), emptyList()) { RunExecutionResult.Completed(diagnosticReport()) }
        }.exceptionOrNull() is IllegalArgumentException)
        val duplicates = RecordingFactory()
        assertTrue(runCatching {
            hub.start(context(), request(), duplicates.create() + duplicates.create()) {
                RunExecutionResult.Completed(diagnosticReport())
            }
        }.exceptionOrNull() is IllegalArgumentException)
        val rejected = RecordingFactory()
        assertFalse(hub.start(context().copy(startAllowed = false), request(), rejected.create()) {
            RunExecutionResult.Completed(diagnosticReport())
        })
        assertTrue(rejected.file().aborted)
        val accepted = RecordingFactory()
        val release = CompletableDeferred<Unit>()
        assertTrue(hub.start(context(), request(), accepted.create()) {
            release.await()
            RunExecutionResult.Completed(diagnosticReport())
        })
        hub.reportSinkFailure(request(), LogSinkFailure(LogSinkFailureKind.OPEN, "synthetic"))
        release.complete(Unit)
        waitFor { hub.state.value.entries.size == 11 }
        assertEquals(1, hub.state.value.entries.count { row ->
            row.level == LogLevel.Warn && row.message == "log sink: OPEN: synthetic"
        })
        assertEquals(RunTerminal.DIAGNOSTIC_COMPLETE, hub.state.value.report?.terminal)
    }

    @Test
    fun committedRowsAndInfoReportShareOneGllog2Order() = runBlocking {
        val factory = RecordingFactory()
        val hub = LogHub(LogDocumentFormatter())
        assertTrue(hub.start(context(), request(), factory.create()) { attach ->
            emitSource(attach) { source ->
                source.emit(LogDraft(LogOrigin.Native, LogLevel.Info, "native-one"))
                source.emit(LogDraft(LogOrigin.Shell, LogLevel.Warn, "shell-two"))
            }
            RunExecutionResult.Completed(diagnosticReport())
        })
        val file = withTimeout(10_000L) { factory.file().finished.await() }
        waitFor { hub.state.value.entries.size == 12 }
        val text = File(file.path).readText()
        assertTrue(text.startsWith("GLLOG2\nmeta\trun_id="))
        assertTrue(text.contains("entry\tcommit_id=2\torigin=Native\tlevel=Info"))
        assertTrue(text.contains("entry\tcommit_id=3\torigin=Shell\tlevel=Warn"))
        assertTrue(text.contains("entry\tcommit_id=12\torigin=Kotlin\tlevel=Info"))
        assertTrue(text.endsWith("session\tphase=Done\nend\tterminal_received=true\n"))
        assertFalse(text.contains("report\t"))
        assertEquals((1L..12L).toList(), hub.state.value.entries.map { it.commitId })
        assertFalse(hub.state.value.report?.isProductSuccess ?: true)
    }

    @Test
    fun concurrentSourcesKeepOwnFifoAndCommitOnlyByRawAcceptance() = runBlocking {
        val factory = RecordingFactory()
        val hub = LogHub(LogDocumentFormatter())
        assertTrue(hub.start(context(), request(), factory.create()) { attach ->
            coroutineScope {
                val first = KotlinLogConnector()
                val second = KotlinLogConnector()
                val firstCollection = async { attach(first) }
                val secondCollection = async { attach(second) }

                first.emit(LogDraft(LogOrigin.Kotlin, LogLevel.Info, "first-a", 400L))
                waitFor { hub.state.value.entries.size >= 2 }
                second.emit(LogDraft(LogOrigin.Native, LogLevel.Info, "second-a", 100L))
                waitFor { hub.state.value.entries.size >= 3 }
                first.emit(LogDraft(LogOrigin.Kotlin, LogLevel.Info, "first-b", 300L))
                waitFor { hub.state.value.entries.size >= 4 }
                second.emit(LogDraft(LogOrigin.Native, LogLevel.Info, "second-b", 0L))
                waitFor { hub.state.value.entries.size >= 5 }

                first.finishInput()
                second.finishInput()
                assertEquals(LogConnectorEnd.Drained, firstCollection.await())
                assertEquals(LogConnectorEnd.Drained, secondCollection.await())
            }
            RunExecutionResult.Completed(diagnosticReport())
        })

        withTimeout(10_000L) { factory.file().finished.await() }
        waitFor { hub.state.value.entries.size == 14 }
        val sourceRows = hub.state.value.entries.filter { row -> row.message.contains("-a") || row.message.contains("-b") }
        assertEquals(listOf("first-a", "second-a", "first-b", "second-b"), sourceRows.map(LogRecord::message))
        assertEquals(listOf(2L, 3L, 4L, 5L), sourceRows.map(LogRecord::commitId))
        assertEquals(listOf(400L, 100L, 300L, 0L), sourceRows.map(LogRecord::sourceElapsedMs))
    }

    @Test
    fun incompleteSourceWarnsThenOtherSourceAndTypedReportStillComplete() = runBlocking {
        val factory = RecordingFactory()
        val hub = LogHub(LogDocumentFormatter())
        val incompleteClosed = AtomicBoolean(false)
        assertTrue(hub.start(context(), request(), factory.create()) { attach ->
            val incomplete = object : LogConnector {
                override suspend fun collect(emit: suspend (LogDraft) -> Unit): LogConnectorEnd {
                    emit(LogDraft(LogOrigin.Native, LogLevel.Info, "accepted-prefix"))
                    return LogConnectorEnd.Incomplete("synthetic-source-failure")
                }

                override fun close() {
                    incompleteClosed.set(true)
                }
            }
            assertTrue(attach(incomplete) is LogConnectorEnd.Incomplete)
            emitSource(attach) { source ->
                source.emit(LogDraft(LogOrigin.Shell, LogLevel.Info, "healthy-source"))
            }
            RunExecutionResult.Completed(diagnosticReport())
        })

        withTimeout(10_000L) { factory.file().finished.await() }
        waitFor { hub.state.value.entries.size == 13 }
        assertTrue(incompleteClosed.get())
        assertEquals(1, hub.state.value.entries.count { row -> row.message == "accepted-prefix" })
        assertEquals(1, hub.state.value.entries.count { row -> row.message == "healthy-source" })
        assertEquals(1, hub.state.value.entries.count { row ->
            row.level == LogLevel.Warn && row.message == "log source incomplete: synthetic-source-failure"
        })
        assertEquals(RunTerminal.DIAGNOSTIC_COMPLETE, hub.state.value.report?.terminal)
    }

    @Test
    fun warnThresholdFiltersAllNineReportInfoRowsWithoutChangingTypedReport() = runBlocking {
        val factory = RecordingFactory()
        val hub = LogHub(LogDocumentFormatter())
        assertTrue(hub.start(context(LogLevel.Warn), request(), factory.create()) { attach ->
            emitSource(attach) { source ->
                source.emit(LogDraft(LogOrigin.Native, LogLevel.Warn, "warn-only"))
            }
            RunExecutionResult.Completed(diagnosticReport())
        })
        val prefix = withTimeout(10_000L) { factory.file().finished.await() }
        waitFor { hub.state.value.entries.size == 1 }
        val text = File(prefix.path).readText()
        assertEquals(listOf("warn-only"), hub.state.value.entries.map { it.message })
        assertFalse(text.contains("Capability:"))
        assertFalse(text.contains("Product success:"))
        assertFalse(text.contains("report\t"))
        assertEquals(RunTerminal.DIAGNOSTIC_COMPLETE, hub.state.value.report?.terminal)
    }

    @Test
    fun blockedMediaSinkCannotDelayOver10MibPrivateFileOrUiTerminal() = runBlocking {
        val factory = RecordingFactory(blockMedia = true)
        val hub = LogHub(LogDocumentFormatter())
        val message = "x".repeat(640)
        assertTrue(hub.start(context(), request(enabled = true), factory.create()) { attach ->
            emitSource(attach) { source ->
                repeat(16_384) { source.emit(LogDraft(LogOrigin.Native, LogLevel.Info, message)) }
            }
            RunExecutionResult.Completed(diagnosticReport())
        })
        val prefix = withTimeout(60_000L) { factory.file().finished.await() }
        waitFor(60_000L) { hub.state.value.entries.size == 16_394 }
        assertTrue(prefix.byteLength > 10L * 1024L * 1024L)
        assertEquals(prefix.byteLength, File(prefix.path).length())
        assertTrue(factory.mediaEvents.get() >= 16_384)
        assertEquals(RunSessionPhase.Done, hub.state.value.phase)
        assertEquals(16_394L, hub.state.value.entries[16_393].commitId)
    }

    @Test
    fun clearAndActiveBarrierPreserveOldSnapshotAndExactFilePrefix() = runBlocking {
        val factory = RecordingFactory(blockMedia = true)
        val hub = LogHub(LogDocumentFormatter())
        val firstEmitted = CompletableDeferred<Unit>()
        val resume = CompletableDeferred<Unit>()
        assertTrue(hub.start(context(), request(), factory.create()) { attach ->
            emitSource(attach) { source ->
                source.emit(LogDraft(LogOrigin.Native, LogLevel.Info, "before-clear"))
                firstEmitted.complete(Unit)
                resume.await()
                source.emit(LogDraft(LogOrigin.Native, LogLevel.Info, "after-clear"))
            }
            RunExecutionResult.Completed(diagnosticReport())
        })
        firstEmitted.await()
        waitFor { hub.state.value.entries.size == 2 }
        val old = hub.state.value.entries
        val prefix = withTimeout(10_000L) { hub.filePrefix()!! }
        val prefixBytes = File(prefix.path).readBytes().copyOf(prefix.byteLength.toInt()).toString(Charsets.UTF_8)
        assertTrue(prefixBytes.contains("before-clear"))
        assertFalse(prefixBytes.contains("after-clear"))
        hub.clearTerminal()
        waitFor { hub.state.value.entries.isEmpty() }
        resume.complete(Unit)
        withTimeout(10_000L) { factory.file().finished.await() }
        waitFor { hub.state.value.entries.size == 10 }
        assertEquals(2, old.size)
        assertEquals("before-clear", old[1].message)
        assertEquals(12L, hub.state.value.entries.last().commitId)
        assertFalse(File(prefix.path).readText().startsWith("GLLOG1"))
        assertEquals(0, factory.mediaControls.get())
    }

    /** Publishing 100k rows fixes old prefixes without cloning the backing array. */
    @Test
    fun hundredThousandRowSnapshotsKeepStablePrefixes() {
        val lock = Any()
        val rows = ArrayList<LogRecord>()
        val old = LogRowsSnapshot.fromBacking(rows, lock, 1L, 1L)
        synchronized(lock) {
            repeat(100_000) { index ->
                rows += LogRecord(LogOrigin.Native, LogLevel.Debug, "row-$index", commitId = index.toLong() + 1L)
            }
        }
        val current = LogRowsSnapshot.fromBacking(rows, lock, 1L, 2L)
        assertEquals(0, old.size)
        assertEquals(100_000, current.size)
        assertEquals(100_000L, current[99_999].commitId)
        assertFalse(old == current)
    }

    private suspend fun waitFor(timeoutMs: Long = 10_000L, predicate: () -> Boolean) {
        withTimeout(timeoutMs) { while (!predicate()) delay(5L) }
    }

    /** Attaches one Kotlin source, stops its write side, and waits for complete prefix drain. */
    private suspend fun emitSource(
        attach: suspend (LogConnector) -> LogConnectorEnd,
        emit: suspend (KotlinLogConnector) -> Unit,
    ) = coroutineScope {
        val source = KotlinLogConnector()
        val collection = async { attach(source) }
        emit(source)
        source.finishInput()
        assertEquals(LogConnectorEnd.Drained, collection.await())
    }

    private fun context(minimum: LogLevel = LogLevel.Info) = RunSessionState(
        backendKey = "DIAGNOSE", backendLabel = "Diagnose", startAllowed = true,
        abortable = true, minimumLogLevel = minimum,
    )

    private fun request(enabled: Boolean = false) = LogSinkRequest(
        enabled = enabled, runId = "0".repeat(32), generation = 1L,
        backendKey = "DIAGNOSE", relativePath = "Download/ghostlock-log",
        formatConfig = LogFormatConfig(false),
    )

    private fun diagnosticReport() = RunReport(
        runCapability = RunCapability.DIAGNOSTIC_ONLY,
        diagnosticOutcome = DiagnosticOutcome.PASS,
        backendPrimitive = StageResult.SUCCESS,
        rootScriptDispatch = StageResult.SKIPPED,
        payloadItems = emptyList(), payloadOverall = StageResult.SKIPPED,
        finishItems = emptyList(), finishOverall = StageResult.SKIPPED,
        backendCleanup = StageResult.SUCCESS,
        terminal = RunTerminal.DIAGNOSTIC_COMPLETE,
    )

    private class RecordingFactory(private val blockMedia: Boolean = false) {
        private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
        private val created = mutableListOf<RecordingFileSink>()
        val mediaEvents = AtomicInteger(0)
        val mediaControls = AtomicInteger(0)
        fun create(): List<LogSinkBinding> {
            val file = RecordingFileSink(scope).also { createdSink -> synchronized(created) { created += createdSink } }
            val bindings = mutableListOf(LogSinkBinding(LogSinkRole.PRIVATE_FILE, file))
            if (blockMedia) bindings += LogSinkBinding(LogSinkRole.MEDIA_STORE, object : LogSink {
                    override fun tryEmit(event: LogSinkEvent): Boolean {
                        mediaEvents.incrementAndGet()
                        if (event is LogSinkEvent.FileBarrier || event == LogSinkEvent.UiClear) {
                            mediaControls.incrementAndGet()
                        }
                        return true
                    }
                    override fun abort(reason: String) = Unit
                })
            return bindings
        }
        fun file(): RecordingFileSink = synchronized(created) { created.last() }
    }

    private class RecordingFileSink(scope: CoroutineScope) : LogFileSink {
        private val queue = Channel<LogSinkEvent>(Channel.UNLIMITED)
        private val target = File.createTempFile("ghostlock-loghub-", ".log")
        private val completion = CompletableDeferred<LogFilePrefix>()
        var aborted: Boolean = false
            private set
        override val finished = completion
        init {
            scope.launch {
                var written = 0L
                target.outputStream().buffered().use { output ->
                    for (event in queue) {
                        when (event) {
                            is LogSinkEvent.Start -> event.rows.forEach { row ->
                                output.write(row); written += row.size
                            }
                            is LogSinkEvent.Entry -> {
                                output.write(event.row); written += event.row.size
                            }
                            is LogSinkEvent.FileBarrier -> {
                                output.flush(); event.completion.complete(LogFilePrefix(target.path, written))
                            }
                            is LogSinkEvent.End -> {
                                event.rows.forEach { row -> output.write(row); written += row.size }
                                output.flush(); completion.complete(LogFilePrefix(target.path, written)); break
                            }
                            LogSinkEvent.UiClear -> Unit
                        }
                    }
                }
            }
        }
        override fun tryEmit(event: LogSinkEvent): Boolean = queue.trySend(event).isSuccess
        override fun abort(reason: String) { aborted = true; queue.close() }
    }
}
