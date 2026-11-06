package com.ghostlock.app.core.run

import android.os.SystemClock
import com.ghostlock.app.domain.run.DiagnosticOutcome
import com.ghostlock.app.domain.run.ExitKind
import com.ghostlock.app.domain.run.RunExecutionResult
import com.ghostlock.app.domain.run.RunLogDocumentFormatter
import com.ghostlock.app.domain.run.RunLogDocumentIdentity
import com.ghostlock.app.domain.run.RunLogDocumentSnapshot
import com.ghostlock.app.domain.run.RunLogFormatConfig
import com.ghostlock.app.domain.run.RunLogLevel
import com.ghostlock.app.domain.run.RunLogOrigin
import com.ghostlock.app.domain.run.RunLogRecord
import com.ghostlock.app.domain.run.RunLogSink
import com.ghostlock.app.domain.run.RunLogSinkFactory
import com.ghostlock.app.domain.run.RunLogSinkFailure
import com.ghostlock.app.domain.run.RunLogSinkFailureKind
import com.ghostlock.app.domain.run.RunLogSinkRequest
import com.ghostlock.app.domain.run.RunReport
import com.ghostlock.app.domain.run.RunSessionPhase
import com.ghostlock.app.domain.run.RunSessionState
import com.ghostlock.app.domain.run.RunTerminal
import com.ghostlock.app.domain.run.StageResult
import com.ghostlock.app.domain.run.StageState
import com.ghostlock.app.domain.run.initialRunLogRecords
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

/**
 * Application-lifetime owner of one typed RunHost client, one canonical document, and one optional run-log sink.
 *
 * @param sinkFactory Platform boundary that immediately returns one non-blocking run-scoped sink.
 * @param formatter Sole pure GLLOG1 formatter shared by streaming export and manual share.
 * @note Canonical commit, formatting, and enqueue share one lock; no second queue or completion wait exists here.
 */
class RunCoordinator(
    private val sinkFactory: RunLogSinkFactory,
    private val formatter: RunLogDocumentFormatter,
) {
    private val lock = Any()
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
    private val mutableState = MutableStateFlow(RunSessionState())
    private var activeJob: Job? = null
    private var activeToken: Long = 0L
    private var abortRequested: Boolean = false
    private var canonicalDocument = CanonicalDocument(
        identity = null,
        formatConfig = RunLogFormatConfig(showTimestamp = false),
        minimumLogLevel = RunLogLevel.Info,
        entries = initialRunLogRecords().toMutableList(),
        phase = RunSessionPhase.Idle,
        report = null,
    )
    private var activeSink: ActiveSink? = null

    /** Immutable application-scoped session stream. */
    val state: StateFlow<RunSessionState> = mutableState.asStateFlow()

    /** True until the current run releases its sole coordinator slot, including abort finalization. */
    fun isRunning(): Boolean = synchronized(lock) {
        activeJob != null
    }

    /**
     * Updates selected backend/readiness facts without taking ownership of an active or completed document.
     *
     * @param context Fresh presentation context from the run-session adapter.
     */
    fun updateContext(context: RunSessionState) {
        synchronized(lock) {
            if (activeJob != null) return
            val current = mutableState.value
            val selectionChanged = current.backendKey != context.backendKey
            mutableState.value = context.copy(
                phase = if (selectionChanged) RunSessionPhase.Idle else current.phase,
                entries = if (selectionChanged) {
                    initialRunLogRecords(context.minimumLogLevel)
                } else {
                    current.entries
                },
                report = if (selectionChanged) null else current.report,
            )
            if (canonicalDocument.identity == null) {
                canonicalDocument = canonicalDocument.copy(
                    formatConfig = RunLogFormatConfig(context.showTimestamp),
                    minimumLogLevel = context.minimumLogLevel,
                    entries = mutableState.value.entries.toMutableList(),
                    phase = mutableState.value.phase,
                    report = mutableState.value.report,
                )
            }
        }
    }

    /**
     * Starts one typed RunHost client coroutine and, when enabled, one non-blocking run-log sink.
     *
     * @param context Gate-approved immutable selected-backend facts.
     * @param sinkRequest Pure accepted-run identity and sink settings frozen with the host request.
     * @param execute Data-adapter call that emits validated log records and returns a typed terminal result.
     * @return True only when the coordinator accepted ownership.
     */
    fun start(
        context: RunSessionState,
        sinkRequest: RunLogSinkRequest,
        execute: suspend ((RunLogRecord) -> Unit) -> RunExecutionResult,
    ): Boolean {
        lateinit var launched: Job
        val token: Long
        val startedAt: Long
        synchronized(lock) {
            if (
                activeJob != null ||
                !context.startAllowed ||
                context.backendKey == null ||
                context.backendKey != sinkRequest.backendKey
            ) {
                return false
            }
            check(activeToken < Long.MAX_VALUE) { "run coordinator token exhausted" }
            activeToken += 1L
            token = activeToken
            abortRequested = false
            startedAt = SystemClock.elapsedRealtime()
            val initialEntries = mutableListOf(
                RunLogRecord(
                    RunLogOrigin.Kotlin,
                    RunLogLevel.Info,
                    "backend run: " + context.backendLabel,
                ),
            ).filterTo(mutableListOf()) { record ->
                record.level.wireValue >= context.minimumLogLevel.wireValue
            }
            val identity = RunLogDocumentIdentity(
                runId = sinkRequest.runId,
                generation = sinkRequest.generation,
                backendKey = sinkRequest.backendKey,
            )
            canonicalDocument = CanonicalDocument(
                identity = identity,
                formatConfig = sinkRequest.formatConfig,
                minimumLogLevel = context.minimumLogLevel,
                entries = initialEntries,
                phase = RunSessionPhase.Running,
                report = null,
            )
            activeSink = null
            mutableState.value = context.copy(
                phase = RunSessionPhase.Running,
                report = null,
                entries = initialEntries.toList(),
            )
            if (sinkRequest.enabled) {
                val sink = runCatching {
                    sinkFactory.create(sinkRequest) { failure -> publishSinkFailure(token, failure) }
                }.getOrElse { problem ->
                    publishSinkFailureLocked(
                        token,
                        RunLogSinkFailure(
                            RunLogSinkFailureKind.OPEN,
                            "automatic export sink creation failed: " + problem::class.java.simpleName,
                        ),
                    )
                    null
                }
                if (sink != null) {
                    activeSink = ActiveSink(token, sink)
                    sink.tryEmit(
                        formatter.formatHeader(
                            identity = identity,
                            records = initialEntries,
                            config = sinkRequest.formatConfig,
                        ),
                    )
                }
            }
            launched = scope.launch(start = CoroutineStart.LAZY) {
                try {
                    val result = execute { record -> appendRecord(token, startedAt, record) }
                    complete(token, startedAt, result)
                } catch (cancelled: CancellationException) {
                    throw cancelled
                } catch (problem: Throwable) {
                    complete(
                        token,
                        startedAt,
                        RunExecutionResult.TransportFailure(
                            "run-coordinator:exception:" + problem::class.java.simpleName,
                        ),
                    )
                } finally {
                    completeAbort(token)
                    synchronized(lock) {
                        if (activeToken == token && activeJob === launched) {
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

    /** Requests explicit typed cancellation for an abortable current backend. */
    fun requestAbort(disabledMessage: String): Boolean {
        var jobToCancel: Job? = null
        val accepted = synchronized(lock) {
            val current = mutableState.value
            val job = activeJob ?: return@synchronized false
            if (!job.isActive || current.phase != RunSessionPhase.Running) return@synchronized false
            if (!current.abortable) {
                val entry = RunLogRecord(
                    RunLogOrigin.Kotlin,
                    RunLogLevel.Warn,
                    disabledMessage,
                )
                if (acceptsLocked(entry.level)) {
                    mutableState.value = current.copy(entries = current.entries + entry)
                    appendCanonicalRecordLocked(entry)
                }
                false
            } else {
                abortRequested = true
                mutableState.value = current.copy(phase = RunSessionPhase.Aborting)
                canonicalDocument.phase = RunSessionPhase.Aborting
                jobToCancel = job
                true
            }
        }
        jobToCancel?.cancel(CancellationException("explicit typed RunHost cancel"))
        return accepted
    }

    /** Clears presentation rows without changing the physical run, canonical document, or terminal report. */
    fun clearTerminal() {
        synchronized(lock) { mutableState.value = mutableState.value.copy(entries = emptyList()) }
    }

    /**
     * Renders one immutable canonical snapshot for manual sharing.
     *
     * @return Complete GLLOG1 UTF-8 bytes with a final LF.
     */
    fun renderCanonicalDocument(): ByteArray {
        val snapshot = synchronized(lock) {
            RunLogDocumentSnapshot(
                identity = canonicalDocument.identity,
                records = canonicalDocument.entries.toList(),
                phase = canonicalDocument.phase,
                report = canonicalDocument.report,
                config = canonicalDocument.formatConfig,
            )
        }
        return formatter.format(snapshot)
    }

    /** Accepts one validated source record only for the current active token. */
    private fun appendRecord(token: Long, startedAt: Long, record: RunLogRecord) {
        synchronized(lock) {
            val current = mutableState.value
            if (token != activeToken || current.phase !in ACTIVE_PHASES) return
            if (!acceptsLocked(record.level)) return
            val accepted = record.copy(elapsedMs = elapsedSince(startedAt))
            mutableState.value = current.copy(entries = current.entries + accepted)
            appendCanonicalRecordLocked(accepted)
        }
    }

    /** Publishes one terminal projection, emits its trailer, and finishes without waiting for the sink. */
    private fun complete(token: Long, startedAt: Long, result: RunExecutionResult) {
        synchronized(lock) {
            if (token != activeToken || abortRequested) return
            val current = mutableState.value
            if (current.phase !in ACTIVE_PHASES) return
            val elapsed = elapsedSince(startedAt)
            val report = (result as? RunExecutionResult.Completed)?.report
            val candidates = when (result) {
                is RunExecutionResult.Blocked -> result.reasons.map { reason ->
                    RunLogRecord(RunLogOrigin.Kotlin, RunLogLevel.Warn, reason, elapsed)
                }

                is RunExecutionResult.Refused -> result.reasons.map { reason ->
                    RunLogRecord(RunLogOrigin.Kotlin, RunLogLevel.Warn, reason, elapsed)
                }

                is RunExecutionResult.TransportFailure -> listOf(
                    RunLogRecord(RunLogOrigin.Kotlin, RunLogLevel.Error, result.reason, elapsed),
                )

                is RunExecutionResult.Completed -> reportRecords(result.report, elapsed)
            }
            val additions = candidates.filter { record -> acceptsLocked(record.level) }
            val phase = if (report?.terminal == RunTerminal.REBOOT_REQUESTED) {
                RunSessionPhase.HandoffRequested
            } else if (result is RunExecutionResult.Blocked) {
                RunSessionPhase.Idle
            } else {
                RunSessionPhase.Done
            }
            mutableState.value = current.copy(
                phase = phase,
                entries = current.entries + additions,
                report = report,
            )
            additions.forEach(::appendCanonicalRecordLocked)
            canonicalDocument.phase = phase
            canonicalDocument.report = report
            canonicalDocument.terminalPublished = true
            activeSinkForTokenLocked(token)?.let { sink ->
                sink.tryEmit(formatter.formatTrailer(phase, report))
                sink.finish()
            }
        }
    }

    /** Publishes explicit abort, emits its trailer, and releases the caller without a drain wait. */
    private fun completeAbort(token: Long) {
        synchronized(lock) {
            if (token != activeToken || !abortRequested || canonicalDocument.terminalPublished) return
            mutableState.value = mutableState.value.copy(phase = RunSessionPhase.Aborted)
            canonicalDocument.phase = RunSessionPhase.Aborted
            canonicalDocument.report = null
            canonicalDocument.terminalPublished = true
            activeSinkForTokenLocked(token)?.let { sink ->
                sink.tryEmit(formatter.formatTrailer(RunSessionPhase.Aborted, null))
                sink.finish()
            }
        }
    }

    /** Adds one canonical record and immediately enqueues its row under the same ordering lock. */
    private fun appendCanonicalRecordLocked(record: RunLogRecord) {
        canonicalDocument.entries += record
        if (canonicalDocument.terminalPublished) return
        activeSinkForTokenLocked(activeToken)?.tryEmit(
            listOf(formatter.formatRecord(record, canonicalDocument.formatConfig)),
        )
    }

    /** @return True when [level] reaches the current run's frozen minimum. */
    private fun acceptsLocked(level: RunLogLevel): Boolean =
        level.wireValue >= canonicalDocument.minimumLogLevel.wireValue

    /** @return Current sink only when it belongs to [token]. */
    private fun activeSinkForTokenLocked(token: Long): RunLogSink? =
        activeSink?.takeIf { runtime -> runtime.token == token }?.sink

    /** Publishes one token-isolated, presentation-only first-failure callback. */
    private fun publishSinkFailure(token: Long, failure: RunLogSinkFailure) {
        synchronized(lock) { publishSinkFailureLocked(token, failure) }
    }

    /** Adds at most one non-canonical export warning for the current accepted run. */
    private fun publishSinkFailureLocked(token: Long, failure: RunLogSinkFailure) {
        if (token != activeToken || canonicalDocument.exportWarningPublished) return
        canonicalDocument.exportWarningPublished = true
        val current = mutableState.value
        mutableState.value = current.copy(
            entries = if (acceptsLocked(RunLogLevel.Warn)) {
                current.entries + RunLogRecord(
                    RunLogOrigin.Kotlin,
                    RunLogLevel.Warn,
                    "automatic export: " + failure.kind.name + ": " + failure.detail,
                )
            } else {
                current.entries
            },
        )
    }

    /** Derives the sole terminal presentation records from the typed report under the hub lock. */
    private fun reportRecords(report: RunReport, elapsed: Long): List<RunLogRecord> = listOf(
        reportRecord("Capability", report.runCapability.name, elapsed),
        reportRecord(
            "Diagnostic",
            report.diagnosticOutcome.name,
            elapsed,
            stateColor(
                success = report.diagnosticOutcome == DiagnosticOutcome.PASS,
                failure = report.diagnosticOutcome == DiagnosticOutcome.FAIL,
            ),
        ),
        stageRecord("Backend primitive", report.backendPrimitive, elapsed),
        stageRecord("Root-script dispatch", report.rootScriptDispatch, elapsed),
        stageRecord("Payload", report.payloadOverall, elapsed, report.payloadItems.size),
        stageRecord("Finish", report.finishOverall, elapsed, report.finishItems.size),
        stageRecord("Backend cleanup", report.backendCleanup, elapsed),
        reportRecord(
            "Terminal",
            report.terminal.name,
            elapsed,
            stateColor(
                success = report.terminal in setOf(RunTerminal.SUCCESS, RunTerminal.DIAGNOSTIC_COMPLETE),
                failure = report.terminal == RunTerminal.FAILED,
                warning = report.terminal in setOf(
                    RunTerminal.DEGRADED,
                    RunTerminal.UNKNOWN,
                    RunTerminal.NEEDS_ATTENTION,
                    RunTerminal.REBOOT_REQUIRED,
                ),
            ),
        ),
        reportRecord(
            "Product success",
            if (report.isProductSuccess) "YES" else "NO",
            elapsed,
            if (report.isProductSuccess) ANSI_GREEN else null,
        ),
    )

    /** Creates one typed stage summary with an optional item count. */
    private fun stageRecord(
        name: String,
        stage: StageResult,
        elapsed: Long,
        itemCount: Int? = null,
    ): RunLogRecord {
        val exit = if (stage.exitKind == ExitKind.NONE) "" else "/${stage.exitKind.name}=${stage.exitValue}"
        val count = itemCount?.let { value -> " ($value)" }.orEmpty()
        val value = "${stage.state.name}/${stage.reason.name}$exit$count"
        return reportRecord(
            name,
            value,
            elapsed,
            stateColor(
                success = stage.state == StageState.SUCCESS,
                failure = stage.state == StageState.FAILED,
                warning = stage.state in setOf(StageState.UNKNOWN, StageState.CANCELLED),
            ),
        )
    }

    /** Creates one normal Info record whose value may carry an approved foreground override. */
    private fun reportRecord(
        name: String,
        value: String,
        elapsed: Long,
        ansiColor: String? = null,
    ): RunLogRecord {
        val rendered = if (ansiColor == null) value else ansiColor + value + ANSI_RESET
        return RunLogRecord(
            origin = RunLogOrigin.Kotlin,
            level = RunLogLevel.Info,
            message = "$name: $rendered",
            elapsedMs = elapsed,
        )
    }

    /** @return Approved ANSI foreground prefix for one semantic result, or null for default Info white. */
    private fun stateColor(
        success: Boolean,
        failure: Boolean,
        warning: Boolean = false,
    ): String? = when {
        success -> ANSI_GREEN
        failure -> ANSI_RED
        warning -> ANSI_YELLOW
        else -> null
    }

    /** Returns non-negative elapsed milliseconds from the monotonic clock. */
    private fun elapsedSince(startedAt: Long): Long =
        (SystemClock.elapsedRealtime() - startedAt).coerceAtLeast(0L)

    /** Mutable canonical owner guarded exclusively by [lock]. */
    private data class CanonicalDocument(
        val identity: RunLogDocumentIdentity?,
        val formatConfig: RunLogFormatConfig,
        val minimumLogLevel: RunLogLevel,
        val entries: MutableList<RunLogRecord>,
        var phase: RunSessionPhase,
        var report: RunReport?,
        var terminalPublished: Boolean = false,
        var exportWarningPublished: Boolean = false,
    )

    /** Run token and its sole non-blocking sink handle. */
    private data class ActiveSink(
        val token: Long,
        val sink: RunLogSink,
    )

    private companion object {
        const val ANSI_RESET: String = "\u001B[0m"
        const val ANSI_RED: String = "\u001B[31m"
        const val ANSI_GREEN: String = "\u001B[32m"
        const val ANSI_YELLOW: String = "\u001B[33m"
        val ACTIVE_PHASES: Set<RunSessionPhase> = setOf(
            RunSessionPhase.Running,
            RunSessionPhase.Aborting,
        )
    }
}
