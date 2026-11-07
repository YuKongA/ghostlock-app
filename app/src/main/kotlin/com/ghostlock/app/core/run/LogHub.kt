package com.ghostlock.app.core.run

import com.ghostlock.app.domain.run.*
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

/**
 * Application-lifetime single-writer log hub: raw FIFO, one commit/format processor, processed FIFO, and one dispatcher.
 * The dispatcher only enqueues the same immutable event into independent unbounded sink queues; no sink I/O blocks it.
 */
class LogHub(
    private val formatter: LogDocumentFormatter,
) {
    private val lock = Any()
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
    private val raw = Channel<RawEvent>(Channel.UNLIMITED)
    private val processed = Channel<ProcessedEvent>(Channel.UNLIMITED)
    private val mutableState = MutableStateFlow(RunSessionState())
    private var activeJob: Job? = null
    private var activeToken = 0L
    private var latestRun: ActiveRun? = null
    private var visibleToken: Long? = null
    private var abortRequested = false
    private var streamFailure: Throwable? = null

    /** Immutable application-scoped session stream. */
    val state: StateFlow<RunSessionState> = mutableState.asStateFlow()

    init {
        scope.launch { processRaw() }
        scope.launch { dispatchProcessed() }
    }

    /** True until the current Flow releases its run slot, including abort finalization. */
    fun isRunning(): Boolean = synchronized(lock) { activeJob != null }

    /** Updates selected backend/readiness without replacing an active or completed log. */
    fun updateContext(context: RunSessionState) {
        synchronized(lock) {
            if (activeJob != null) return
            val current = mutableState.value
            val changed = current.backendKey != context.backendKey
            if (changed) visibleToken = null
            mutableState.value = context.copy(
                phase = if (changed) RunSessionPhase.Idle else current.phase,
                entries = if (changed) {
                    LogRowsSnapshot.fromRecords(initialLogRecords(context.minimumLogLevel), activeToken, 0L)
                } else current.entries,
                report = if (changed) null else current.report,
            )
        }
    }

    /** Accepts one Flow with already-created run-scoped storage sinks; rejected sinks are aborted without I/O. */
    fun start(
        context: RunSessionState,
        sinkRequest: LogSinkRequest,
        storageSinks: List<LogSinkBinding>,
        execute: suspend ((LogRecord) -> Unit) -> RunExecutionResult,
    ): Boolean {
        lateinit var launched: Job
        val suppliedSinks = storageSinks.toList()
        synchronized(lock) {
            if (activeJob != null || streamFailure != null || !context.startAllowed ||
                context.backendKey == null || context.backendKey != sinkRequest.backendKey
            ) {
                suppliedSinks.forEach { it.sink.abort("log run was not accepted") }
                return false
            }
            val roles = suppliedSinks.map(LogSinkBinding::role)
            if (roles.size != roles.toSet().size || roles.count { it == LogSinkRole.PRIVATE_FILE } != 1 ||
                LogSinkRole.UI in roles || suppliedSinks.any { binding ->
                    binding.role == LogSinkRole.PRIVATE_FILE && binding.sink !is LogFileSink
                }
            ) {
                suppliedSinks.forEach { it.sink.abort("invalid storage sink list") }
                throw IllegalArgumentException("one private-file sink and at most one MediaStore sink are required")
            }
            check(activeToken < Long.MAX_VALUE) { "log hub token exhausted" }
            val token = ++activeToken
            val startedAt = System.nanoTime() / 1_000_000L
            abortRequested = false
            val ui = runCatching { UiSink(token, scope, ::publishUiSnapshot) }.getOrElse { problem ->
                suppliedSinks.forEach { it.sink.abort("UI log sink creation failed") }
                throw problem
            }
            val sinks = listOf(LogSinkBinding(LogSinkRole.UI, ui)) + suppliedSinks
            val run = ActiveRun(token, startedAt, context.minimumLogLevel, sinkRequest, sinks)
            latestRun = run
            visibleToken = token
            mutableState.value = context.copy(
                phase = RunSessionPhase.Running,
                report = null,
                entries = LogRowsSnapshot.fromRecords(emptyList(), token, 0L),
            )
            enqueue(RawEvent.Start(run))
            enqueue(RawEvent.Record(token, LogRecord(LogOrigin.Kotlin, LogLevel.Info, "backend run: ${context.backendLabel}")))
            launched = scope.launch(start = CoroutineStart.LAZY) {
                try {
                    val result = execute { record -> appendRecord(token, record) }
                    complete(token, result)
                } catch (cancelled: CancellationException) {
                    throw cancelled
                } catch (problem: Throwable) {
                    complete(token, RunExecutionResult.TransportFailure(
                        "log-hub:exception:${problem::class.java.simpleName}",
                    ))
                } finally {
                    completeAbort(token)
                    synchronized(lock) {
                        if (latestRun?.token == token && activeJob === launched) {
                            activeJob = null
                            abortRequested = false
                        }
                    }
                }
            }
            activeJob = launched
        }
        launched.start()
        return true
    }

    /** Accepts a storage worker's first failure only for the matching active run. */
    fun reportSinkFailure(request: LogSinkRequest, failure: LogSinkFailure) {
        synchronized(lock) {
            val run = latestRun ?: return
            if (run.request == request) publishSinkFailure(run.token, failure)
        }
    }

    /** Requests explicit typed cancellation without touching the physical sink workers. */
    fun requestAbort(disabledMessage: String): Boolean {
        var jobToCancel: Job? = null
        val accepted = synchronized(lock) {
            val run = latestRun ?: return@synchronized false
            val current = mutableState.value
            val job = activeJob ?: return@synchronized false
            if (!job.isActive || current.phase != RunSessionPhase.Running) return@synchronized false
            if (!current.abortable) {
                enqueue(RawEvent.Record(run.token, LogRecord(LogOrigin.Kotlin, LogLevel.Warn, disabledMessage)))
                false
            } else {
                abortRequested = true
                mutableState.value = current.copy(phase = RunSessionPhase.Aborting)
                jobToCancel = job
                true
            }
        }
        jobToCancel?.cancel(CancellationException("explicit typed RunHost cancel"))
        return accepted
    }

    /** Clears UI rows at their FIFO position; private and MediaStore files remain untouched. */
    fun clearTerminal() {
        synchronized(lock) {
            val run = latestRun
            if (run == null || visibleToken != run.token) {
                mutableState.value = mutableState.value.copy(entries = LogRowsSnapshot.fromRecords(emptyList()))
            } else {
                enqueue(RawEvent.UiClear(run.token))
            }
        }
    }

    /**
     * Captures the exact flushed private-file prefix without copying the complete document into memory.
     * Active runs use an ordered barrier; completed runs wait only on their file worker's terminal drain.
     */
    suspend fun filePrefix(): LogFilePrefix? {
        val pending = synchronized(lock) {
            val run = latestRun?.takeIf { it.token == visibleToken } ?: return@synchronized null
            if (run.terminalEnqueued) {
                FileWait.Terminal(run.fileSink)
            } else {
                val completion = CompletableDeferred<LogFilePrefix>()
                enqueue(RawEvent.FileBarrier(run.token, completion))
                FileWait.Barrier(completion)
            }
        } ?: return null
        return when (pending) {
            is FileWait.Barrier -> pending.completion.await()
            is FileWait.Terminal -> pending.sink.finished.await()
        }
    }

    /** Accepts one source record only while the matching run remains active. */
    private fun appendRecord(token: Long, record: LogRecord) {
        synchronized(lock) {
            val run = latestRun ?: return
            if (run.token != token || run.terminalEnqueued || mutableState.value.phase !in ACTIVE_PHASES) return
            enqueue(RawEvent.Record(token, record))
        }
    }

    /** Enqueues ordinary report Info rows and End before releasing the old active slot. */
    private fun complete(token: Long, result: RunExecutionResult) {
        synchronized(lock) {
            val run = latestRun ?: return
            if (run.token != token || run.terminalEnqueued || abortRequested) return
            val report = (result as? RunExecutionResult.Completed)?.report
            val candidates = when (result) {
                is RunExecutionResult.Blocked -> result.reasons.map { LogRecord(LogOrigin.Kotlin, LogLevel.Warn, it) }
                is RunExecutionResult.Refused -> result.reasons.map { LogRecord(LogOrigin.Kotlin, LogLevel.Warn, it) }
                is RunExecutionResult.TransportFailure -> listOf(LogRecord(LogOrigin.Kotlin, LogLevel.Error, result.reason))
                is RunExecutionResult.Completed -> reportRecords(result.report)
            }
            candidates.forEach { enqueue(RawEvent.Record(token, it)) }
            val phase = when {
                report?.terminal == RunTerminal.REBOOT_REQUESTED -> RunSessionPhase.HandoffRequested
                result is RunExecutionResult.Blocked -> RunSessionPhase.Idle
                else -> RunSessionPhase.Done
            }
            run.terminalEnqueued = true
            mutableState.value = mutableState.value.copy(phase = phase, report = report)
            enqueue(RawEvent.End(token, phase))
        }
    }

    /** Guarantees End for cancellation or an unexpected Flow exit. */
    private fun completeAbort(token: Long) {
        synchronized(lock) {
            val run = latestRun ?: return
            if (run.token != token || run.terminalEnqueued) return
            run.terminalEnqueued = true
            mutableState.value = mutableState.value.copy(phase = RunSessionPhase.Aborted, report = null)
            enqueue(RawEvent.End(token, RunSessionPhase.Aborted))
        }
    }

    /** Latches one sink warning as an ordinary Warn record while the run can still accept rows. */
    private fun publishSinkFailure(token: Long, failure: LogSinkFailure) {
        synchronized(lock) {
            val run = latestRun ?: return
            if (run.token != token || run.warningPublished || run.terminalEnqueued) return
            run.warningPublished = true
            enqueue(RawEvent.Record(token, LogRecord(
                LogOrigin.Kotlin, LogLevel.Warn, "log sink: ${failure.kind}: ${failure.detail}",
            )))
        }
    }

    /** Single raw FIFO consumer assigns total order and encodes each accepted row once. */
    private suspend fun processRaw() {
        var current: ActiveRun? = null
        var nextCommitId = 0L
        try {
            for (event in raw) {
                when (event) {
                    is RawEvent.Start -> {
                        current = event.run
                        nextCommitId = 0L
                        val identity = LogDocumentIdentity(
                            event.run.request.runId, event.run.request.generation, event.run.request.backendKey,
                        )
                        processed.trySend(ProcessedEvent.Start(event.run, LogSinkEvent.Start(formatter.formatHeader(identity)))).getOrThrow()
                    }
                    is RawEvent.Record -> {
                        val run = current ?: continue
                        if (run.token != event.token || event.record.level.wireValue < run.minimumLevel.wireValue) continue
                        check(nextCommitId < Long.MAX_VALUE) { "log commit id exhausted" }
                        val committed = event.record.copy(
                            commitId = ++nextCommitId,
                            elapsedMs = elapsedSince(run.startedAt),
                        )
                        val row = formatter.formatRecord(committed, run.request.formatConfig)
                        processed.trySend(ProcessedEvent.ToSink(run.token, LogSinkEvent.Entry(committed, row))).getOrThrow()
                    }
                    is RawEvent.UiClear -> processed.trySend(
                        ProcessedEvent.ToSink(event.token, LogSinkEvent.UiClear),
                    ).getOrThrow()
                    is RawEvent.FileBarrier -> processed.trySend(
                        ProcessedEvent.ToSink(event.token, LogSinkEvent.FileBarrier(event.completion)),
                    ).getOrThrow()
                    is RawEvent.End -> {
                        if (current?.token == event.token) {
                            processed.trySend(ProcessedEvent.ToSink(
                                event.token, LogSinkEvent.End(formatter.formatTrailer(event.phase)),
                            )).getOrThrow()
                            current = null
                        }
                    }
                }
            }
        } catch (problem: Throwable) {
            failStream(problem)
        } finally {
            processed.close()
        }
    }

    /** Single fast fan-out consumer never calls write, flush, close, or a blocking send. */
    private suspend fun dispatchProcessed() {
        var current: ActiveRun? = null
        var last: ActiveRun? = null
        try {
            for (event in processed) {
                when (event) {
                    is ProcessedEvent.Start -> {
                        last?.sinks?.firstOrNull { it.role == LogSinkRole.UI }
                            ?.sink?.abort("new run replaced old UI history")
                        current = event.run
                        last = event.run
                        fanOut(event.run, event.payload)
                    }
                    is ProcessedEvent.ToSink -> {
                        val run = current?.takeIf { it.token == event.token }
                            ?: last?.takeIf { it.token == event.token }
                        if (run == null) {
                            if (event.payload is LogSinkEvent.FileBarrier) {
                                event.payload.completion.completeExceptionally(IllegalStateException("log run detached"))
                            }
                            continue
                        }
                        fanOut(run, event.payload)
                        if (event.payload is LogSinkEvent.End && current === run) current = null
                    }
                }
            }
        } catch (problem: Throwable) {
            failStream(problem)
        } finally {
            current?.let { abortSinks(it, "processed dispatcher stopped") }
        }
    }

    /** Fans out one encoded row reference to registered sinks, routing control events by role. */
    private fun fanOut(run: ActiveRun, event: LogSinkEvent) {
        val target = when (event) {
            LogSinkEvent.UiClear -> LogSinkRole.UI
            is LogSinkEvent.FileBarrier -> LogSinkRole.PRIVATE_FILE
            else -> null
        }
        run.sinks.forEach { binding ->
            if (target == null || binding.role == target) {
                val accepted = binding.sink.tryEmit(event)
                if (!accepted && event is LogSinkEvent.FileBarrier) {
                    event.completion.completeExceptionally(IllegalStateException("private log sink unavailable"))
                }
            }
        }
    }

    /** Stops accepting after an unexpected processor/dispatcher exit and fails pending barriers. */
    private fun failStream(problem: Throwable) {
        synchronized(lock) {
            if (streamFailure != null) return
            streamFailure = problem
            raw.close(problem)
            processed.close(problem)
            latestRun?.let { abortSinks(it, "log stream failed") }
        }
        while (true) {
            val event = raw.tryReceive().getOrNull() ?: break
            if (event is RawEvent.FileBarrier) event.completion.completeExceptionally(problem)
        }
        while (true) {
            val event = processed.tryReceive().getOrNull() ?: break
            if (event is ProcessedEvent.ToSink && event.payload is LogSinkEvent.FileBarrier) {
                event.payload.completion.completeExceptionally(problem)
            }
        }
    }

    /** Aborts all sink workers belonging to a failed stream. */
    private fun abortSinks(run: ActiveRun, reason: String) {
        run.sinks.forEach { binding -> binding.sink.abort(reason) }
    }

    /** Publishes a token-isolated O(1) UI view without copying historical rows. */
    private fun publishUiSnapshot(token: Long, rows: LogRowsSnapshot) {
        synchronized(lock) {
            if (latestRun?.token == token && visibleToken == token) {
                mutableState.value = mutableState.value.copy(entries = rows)
            }
        }
    }

    /** @return Non-negative run-relative time from a monotonic clock. */
    private fun elapsedSince(startedAt: Long): Long =
        (System.nanoTime() / 1_000_000L - startedAt).coerceAtLeast(0L)

    /** Nine ordinary Info log records; the frozen global threshold filters them like every other source. */
    private fun reportRecords(report: RunReport): List<LogRecord> = listOf(
        reportRecord("Capability", report.runCapability.name),
        reportRecord("Diagnostic", report.diagnosticOutcome.name,
            stateColor(report.diagnosticOutcome == DiagnosticOutcome.PASS, report.diagnosticOutcome == DiagnosticOutcome.FAIL)),
        stageRecord("Backend primitive", report.backendPrimitive),
        stageRecord("Root-script dispatch", report.rootScriptDispatch),
        stageRecord("Payload", report.payloadOverall, report.payloadItems.size),
        stageRecord("Finish", report.finishOverall, report.finishItems.size),
        stageRecord("Backend cleanup", report.backendCleanup),
        reportRecord("Terminal", report.terminal.name,
            stateColor(report.terminal in setOf(RunTerminal.SUCCESS, RunTerminal.DIAGNOSTIC_COMPLETE),
                report.terminal == RunTerminal.FAILED,
                report.terminal in setOf(RunTerminal.DEGRADED, RunTerminal.UNKNOWN,
                    RunTerminal.NEEDS_ATTENTION, RunTerminal.REBOOT_REQUIRED))),
        reportRecord("Product success", if (report.isProductSuccess) "YES" else "NO",
            if (report.isProductSuccess) ANSI_GREEN else null),
    )

    /** Converts one typed stage into a single ordinary Info summary. */
    private fun stageRecord(name: String, stage: StageResult, count: Int? = null): LogRecord {
        val exit = if (stage.exitKind == ExitKind.NONE) "" else "/${stage.exitKind.name}=${stage.exitValue}"
        val items = count?.let { " ($it)" }.orEmpty()
        return reportRecord(name, "${stage.state.name}/${stage.reason.name}$exit$items",
            stateColor(stage.state == StageState.SUCCESS, stage.state == StageState.FAILED,
                stage.state in setOf(StageState.UNKNOWN, StageState.CANCELLED)))
    }

    /** Creates one Kotlin/Info record with an optional approved foreground override. */
    private fun reportRecord(name: String, value: String, color: String? = null): LogRecord =
        LogRecord(LogOrigin.Kotlin, LogLevel.Info,
            "$name: " + if (color == null) value else color + value + ANSI_RESET)

    /** @return Approved semantic foreground override, or null for ordinary Info white. */
    private fun stateColor(success: Boolean, failure: Boolean, warning: Boolean = false): String? = when {
        success -> ANSI_GREEN
        failure -> ANSI_RED
        warning -> ANSI_YELLOW
        else -> null
    }

    /** One accepted-run identity and its independent sink owners. */
    private class ActiveRun(
        val token: Long,
        val startedAt: Long,
        val minimumLevel: LogLevel,
        val request: LogSinkRequest,
        val sinks: List<LogSinkBinding>,
        var terminalEnqueued: Boolean = false,
        var warningPublished: Boolean = false,
    ) {
        val fileSink: LogFileSink = sinks.single { it.role == LogSinkRole.PRIVATE_FILE }.sink as LogFileSink
    }

    /** Raw operations share one FIFO with records, clear, barrier, and terminal. */
    private sealed interface RawEvent {
        data class Start(val run: ActiveRun) : RawEvent
        data class Record(val token: Long, val record: LogRecord) : RawEvent
        data class UiClear(val token: Long) : RawEvent
        data class FileBarrier(val token: Long, val completion: CompletableDeferred<LogFilePrefix>) : RawEvent
        data class End(val token: Long, val phase: RunSessionPhase) : RawEvent
    }

    /** One immutable processed event is forwarded to each relevant sink without reformatting. */
    private sealed interface ProcessedEvent {
        data class Start(val run: ActiveRun, val payload: LogSinkEvent.Start) : ProcessedEvent
        data class ToSink(val token: Long, val payload: LogSinkEvent) : ProcessedEvent
    }

    /** File wait never runs on the processor, dispatcher, or Flow completion path. */
    private sealed interface FileWait {
        data class Barrier(val completion: CompletableDeferred<LogFilePrefix>) : FileWait
        data class Terminal(val sink: LogFileSink) : FileWait
    }

    /** UI sink owns one append-only array, publishes only fixed-prefix snapshots, and does no file I/O. */
    private class UiSink(
        private val token: Long,
        scope: CoroutineScope,
        private val publish: (Long, LogRowsSnapshot) -> Unit,
    ) : LogSink {
        private val events = Channel<LogSinkEvent>(Channel.UNLIMITED)
        private val rowsLock = Any()
        private var rows = ArrayList<LogRecord>()
        private var revision = 0L
        private val worker = scope.launch {
            for (event in events) {
                when (event) {
                    is LogSinkEvent.Entry -> {
                        val batch = ArrayList<LogRecord>()
                        batch.add(event.record)
                        var control: LogSinkEvent? = null
                        while (true) {
                            val next = events.tryReceive().getOrNull() ?: break
                            if (next !is LogSinkEvent.Entry) {
                                control = next
                                break
                            }
                            batch.add(next.record)
                        }
                        synchronized(rowsLock) { rows.addAll(batch) }
                        publishRows()
                        control?.let(::handleControl)
                    }
                    else -> handleControl(event)
                }
            }
        }

        override fun tryEmit(event: LogSinkEvent): Boolean = events.trySend(event).isSuccess

        override fun abort(reason: String) {
            events.cancel(CancellationException(reason))
            worker.cancel()
        }

        /** Applies a non-entry event at exactly its FIFO position. */
        private fun handleControl(event: LogSinkEvent) {
            when (event) {
                is LogSinkEvent.Start -> Unit
                LogSinkEvent.UiClear -> {
                    synchronized(rowsLock) { rows = ArrayList() }
                    publishRows()
                }
                is LogSinkEvent.End -> publishRows()
                is LogSinkEvent.Entry, is LogSinkEvent.FileBarrier -> Unit
            }
        }

        /** Publishes only O(1) scalar identity and a fixed-size view of the backing array. */
        private fun publishRows() {
            check(revision < Long.MAX_VALUE) { "UI log revision exhausted" }
            revision += 1L
            publish(token, LogRowsSnapshot.fromBacking(rows, rowsLock, token, revision))
        }
    }

    /** Nonblocking enqueue under the run lock; a closed stream is already a failed runtime. */
    private fun enqueue(event: RawEvent) {
        val sent = raw.trySend(event)
        if (sent.isFailure && event is RawEvent.FileBarrier) {
            event.completion.completeExceptionally(IllegalStateException("log stream closed"))
        }
    }

    private companion object {
        private const val ANSI_RESET = "\u001B[0m"
        private const val ANSI_RED = "\u001B[31m"
        private const val ANSI_GREEN = "\u001B[32m"
        private const val ANSI_YELLOW = "\u001B[33m"
        private val ACTIVE_PHASES = setOf(RunSessionPhase.Running, RunSessionPhase.Aborting)
    }
}
