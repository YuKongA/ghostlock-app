package com.ghostlock.app.core.runhost

import com.ghostlock.app.core.flow.BackendFlowInput
import com.ghostlock.app.core.flow.Flow
import com.ghostlock.app.core.flow.FlowReportContext
import com.ghostlock.app.core.flow.FlowTerminalClaim
import com.ghostlock.app.core.flow.PublishedFlowArtifact
import com.ghostlock.app.core.flow.completeFailure
import com.ghostlock.app.core.log.KotlinLogConnector
import com.ghostlock.app.core.rootscript.RootScriptArtifact
import com.ghostlock.app.domain.backend.AbortPolicy
import com.ghostlock.app.domain.run.DiagnosticOutcome
import com.ghostlock.app.domain.run.ExitKind
import com.ghostlock.app.domain.run.LogConnectorEnd
import com.ghostlock.app.domain.run.LogDraft
import com.ghostlock.app.domain.run.ProfileSnapshot
import com.ghostlock.app.domain.run.ResultReason
import com.ghostlock.app.domain.run.RunCancellationView
import com.ghostlock.app.domain.run.RunCapability
import com.ghostlock.app.domain.run.RunReport
import com.ghostlock.app.domain.run.RunTerminal
import com.ghostlock.app.domain.run.StageResult
import com.ghostlock.app.domain.run.StageState
import java.util.concurrent.CompletableFuture
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicLong
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.async
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.launch

/** Transport-neutral callback boundary owned by one RunHost controller. */
internal interface RunHostEventSink {
    fun publishLog(
        identity: RunHostIdentity,
        sequence: Long,
        draft: LogDraft,
    ): Boolean

    fun publishTerminal(snapshot: RunHostSnapshotModel): Boolean
}

/** Narrow physical workspace lifetime used without exposing Android to the controller. */
internal interface RunHostWorkspaceLease {
    fun activateExecutionEnvironment()

    fun deleteAfterTerminal(): Boolean
}

/** Idempotent plugin execution owner that must end before its workspace can be removed. */
internal fun interface RunHostExecutionLease : AutoCloseable {
    override fun close()
}

/** Fully materialized candidate whose physical resources have transferred to the controller. */
internal data class RunHostPreparedCandidate(
    val identity: RunHostIdentity,
    val abortPolicy: AbortPolicy,
    val flow: Flow,
    val executionLease: RunHostExecutionLease,
    val workspace: RunHostWorkspaceLease,
    val rootScript: RootScriptArtifact,
    val profile: ProfileSnapshot,
    val artifacts: Map<String, PublishedFlowArtifact>,
    val persistentStateDirectory: String,
    val reportContext: FlowReportContext,
)

/** Total materialization result returned by the Android preparation adapter. */
internal sealed interface RunHostCandidateResult {
    data class Prepared(val candidate: RunHostPreparedCandidate) : RunHostCandidateResult

    data class Rejected(
        val code: RunHostCode,
        val pluginTainted: Boolean = false,
    ) : RunHostCandidateResult {
        init {
            require(code != RunHostCode.OK && code != RunHostCode.ALREADY_APPLIED) {
                "materialization rejection must be a failure code"
            }
        }
    }
}

/** Transport-neutral snapshot result with the same OK/non-null invariant as the wire reply. */
internal data class RunHostSnapshotResult(
    val code: RunHostCode,
    val snapshot: RunHostSnapshotModel?,
) {
    init {
        require((code == RunHostCode.OK) == (snapshot != null)) {
            "snapshot must exist exactly for an OK result"
        }
    }
}

/** Sole lifecycle, candidate, worker, callback, and quiescence owner shared by both framework entries. */
internal class RunHostController(
    private val scope: CoroutineScope = CoroutineScope(SupervisorJob() + Dispatchers.Default),
) {
    private val lock = Any()
    private val lifecycle = RunHostStateMachine()
    private val cancellation = AtomicBoolean(false)
    private val logSequence = AtomicLong(0L)
    private val droppedLogs = AtomicLong(0L)
    private var eventSink: RunHostEventSink? = null
    private var prepared: RunHostPreparedCandidate? = null
    private var snapshot: RunHostSnapshotModel? = null
    private var rejectedPluginTainted: Boolean = false
    private var inFlightPhysicalOperations: Int = 0
    private var currentEpochCompletion: CompletableFuture<Unit>? = null

    /** Replaces the callback only when the stable lifecycle contract accepts the identity. */
    fun setCallback(identity: RunHostIdentity, sink: RunHostEventSink): RunHostCode = synchronized(lock) {
        if (rejectedPluginTainted) return@synchronized RunHostCode.HOST_DIED
        lifecycle.setCallback(identity).also { code ->
            if (code == RunHostCode.OK) eventSink = sink
        }
    }

    /** Validates, materializes outside the mutex, and atomically publishes one prepared candidate. */
    fun prepare(
        model: RunHostRequestModel,
        materialize: (RunHostRequestModel, RunHostIdentity) -> RunHostCandidateResult,
    ): RunHostCode {
        lateinit var identity: RunHostIdentity
        lateinit var fingerprint: String
        val preflight = synchronized(lock) {
            if (rejectedPluginTainted) return@synchronized RunHostCode.HOST_DIED
            when (val structural = RunHostRequestValidator.validateStructure(model)) {
                RunHostRequestValidation.Valid -> Unit
                is RunHostRequestValidation.Invalid -> return@synchronized structural.code
            }
            identity = runCatching { RunHostIdentity(model.runId, model.generation) }
                .getOrElse { return@synchronized RunHostCode.INVALID_REQUEST }
            fingerprint = model.semanticFingerprint()
            lifecycle.preparePreflight(identity, fingerprint).also { code ->
                if (code == RunHostCode.OK) beginInFlightLocked()
            }
        }
        if (preflight != RunHostCode.OK) return preflight

        val result = runCatching { materialize(model, identity) }
            .getOrElse { RunHostCandidateResult.Rejected(RunHostCode.INTERNAL_CONTRACT_FAILURE) }
        if (result is RunHostCandidateResult.Rejected) {
            if (result.pluginTainted) synchronized(lock) { rejectedPluginTainted = true }
            finishInFlight()
            return result.code
        }

        val candidate = (result as RunHostCandidateResult.Prepared).candidate
        if (!candidateMatches(candidate, identity)) {
            try {
                synchronized(lock) { rejectedPluginTainted = true }
                releaseCandidate(candidate, deleteWorkspace = true)
            } finally {
                finishInFlight()
            }
            return RunHostCode.INTERNAL_CONTRACT_FAILURE
        }

        val code = synchronized(lock) {
            lifecycle.prepare(identity, fingerprint).also { committed ->
                if (committed == RunHostCode.OK) {
                    prepared = candidate
                    snapshot = preparedSnapshot(candidate)
                }
            }
        }
        try {
            if (code != RunHostCode.OK) releaseCandidate(candidate, deleteWorkspace = true)
        } finally {
            finishInFlight()
        }
        return code
    }

    /** Starts the unique worker after candidate/state ownership has been checked atomically. */
    fun start(identity: RunHostIdentity): RunHostCode {
        var candidate: RunHostPreparedCandidate? = null
        val code = synchronized(lock) {
            if (rejectedPluginTainted) return@synchronized RunHostCode.HOST_DIED
            if (lifecycle.state() == RunHostState.PREPARED && prepared == null) {
                return@synchronized RunHostCode.INTERNAL_CONTRACT_FAILURE
            }
            lifecycle.start(identity).also { transition ->
                if (transition == RunHostCode.OK) {
                    candidate = checkNotNull(prepared)
                    beginInFlightLocked()
                }
                updateLastCodeLocked(transition)
            }
        }
        if (code != RunHostCode.OK) return code

        return try {
            val entered = AtomicBoolean(false)
            val current = checkNotNull(candidate)
            val job = scope.launch {
                entered.set(true)
                runPrepared(current)
            }
            job.invokeOnCompletion {
                if (entered.compareAndSet(false, true)) completeLaunchFailure(current)
            }
            code
        } catch (problem: Throwable) {
            completeLaunchFailure(checkNotNull(candidate))
            RunHostCode.INTERNAL_CONTRACT_FAILURE
        }
    }

    /** Returns the immutable current snapshot without exposing transport types. */
    fun snapshot(identity: RunHostIdentity): RunHostSnapshotResult = synchronized(lock) {
        if (rejectedPluginTainted) {
            return@synchronized RunHostSnapshotResult(RunHostCode.HOST_DIED, null)
        }
        val code = lifecycle.snapshotCode(identity)
        val model = if (code == RunHostCode.OK) snapshot ?: unresolvedSnapshot(identity) else null
        RunHostSnapshotResult(code, model)
    }

    /** Applies cancellation without letting close or destroy overtake physical stop/materialization work. */
    fun cancel(identity: RunHostIdentity): RunHostCode {
        var flowToStop: Flow? = null
        var waitFor: CompletableFuture<Unit>? = null
        var terminal: RunHostSnapshotModel? = null
        var sink: RunHostEventSink? = null
        var executionToClose: RunHostPreparedCandidate? = null
        val code = synchronized(lock) {
            if (rejectedPluginTainted) return@synchronized RunHostCode.HOST_DIED
            val before = lifecycle.state()
            val current = prepared
            val runningCancellationSafe = current?.abortPolicy == AbortPolicy.SAFE_DURING_RUN
            lifecycle.cancel(identity, runningCancellationSafe).also { transition ->
                if (transition == RunHostCode.OK) {
                    cancellation.set(true)
                    if (before == RunHostState.RUNNING) {
                        val running = checkNotNull(current) { "RUNNING host has no prepared candidate" }
                        beginInFlightLocked()
                        flowToStop = running.flow
                    } else {
                        if (current != null) {
                            beginInFlightLocked()
                            executionToClose = current
                        }
                        terminal = cancelledSnapshot(identity, current, before)
                        snapshot = terminal
                        sink = eventSink
                        waitFor = currentEpochCompletion
                    }
                } else {
                    updateLastCodeLocked(transition)
                }
            }
        }

        executionToClose?.let { candidate ->
            try {
                releaseExecution(candidate.executionLease)
            } finally {
                finishInFlight()
            }
        }
        flowToStop?.let { flow ->
            try {
                runCatching { flow.stop() }
            } finally {
                finishInFlight()
            }
        }
        waitFor?.join()
        if (terminal != null && sink != null) publishTerminal(sink, checkNotNull(terminal))
        return code
    }

    /** Closes only after every materializer, worker finalizer, safe stop, and cleanup owner is quiescent. */
    fun requestClose(identity: RunHostIdentity): RunHostCode {
        while (true) {
            var waitFor: CompletableFuture<Unit>? = null
            var candidateToRelease: RunHostPreparedCandidate? = null
            var deleteWorkspace = false
            var immediate: RunHostCode? = null
            synchronized(lock) {
                val state = lifecycle.state()
                if (state == RunHostState.DESTROYED) {
                    if (inFlightPhysicalOperations > 0) {
                        waitFor = checkNotNull(currentEpochCompletion)
                    } else {
                        immediate = lifecycle.requestClose(identity)
                    }
                } else {
                    val identityCode = lifecycle.snapshotCode(identity)
                    if (identityCode != RunHostCode.OK) {
                        immediate = identityCode
                    } else if (state == RunHostState.RUNNING) {
                        immediate = lifecycle.requestClose(identity)
                        updateLastCodeLocked(checkNotNull(immediate))
                    } else if (inFlightPhysicalOperations > 0) {
                        waitFor = checkNotNull(currentEpochCompletion)
                    } else {
                        val code = lifecycle.requestClose(identity)
                        immediate = code
                        if (code == RunHostCode.OK) {
                            val current = prepared
                            val retain = snapshot?.terminal?.requiresWorkspaceRetention() == true
                            prepared = null
                            eventSink = null
                            snapshot = snapshot?.copy(state = RunHostState.DESTROYED, lastCode = code)
                            if (current != null) {
                                beginInFlightLocked()
                                candidateToRelease = current
                                deleteWorkspace = !retain
                            }
                        } else {
                            updateLastCodeLocked(code)
                        }
                    }
                }
            }

            if (waitFor != null) {
                waitFor.join()
                continue
            }
            if (candidateToRelease != null) {
                try {
                    releaseCandidate(checkNotNull(candidateToRelease), deleteWorkspace)
                } finally {
                    finishInFlight()
                }
            }
            return checkNotNull(immediate)
        }
    }

    /** Allows framework process termination only after explicit close and physical quiescence. */
    fun frameworkDestroyAuthorized(): Boolean = synchronized(lock) {
        lifecycle.frameworkDestroyAuthorized() && inFlightPhysicalOperations == 0
    }

    /** Runs one Flow, drains its source, commits terminal facts, then notifies outside the mutex. */
    private suspend fun runPrepared(current: RunHostPreparedCandidate) {
        var report: RunReport? = null
        val source = KotlinLogConnector()
        val terminalClaimed = AtomicBoolean(false)
        val terminalCancelled = AtomicBoolean(false)
        try {
            coroutineScope {
                val collector = async {
                    source.collect { draft -> publishLog(current.identity, draft) }
                }
                val input = BackendFlowInput(
                    runId = current.identity.runId,
                    generation = current.identity.generation,
                    rootScript = current.rootScript,
                    profile = current.profile,
                    logs = source,
                    cancellation = RunCancellationView { cancellation.get() },
                    reportContext = current.reportContext,
                    terminalClaim = FlowTerminalClaim {
                        synchronized(lock) { lifecycle.claimTerminal(current.identity) }.also { cancelled ->
                            if (cancelled != null) {
                                terminalCancelled.set(cancelled)
                                terminalClaimed.set(true)
                            }
                        }
                    },
                    artifacts = current.artifacts,
                    persistentStateDirectory = current.persistentStateDirectory,
                )
                report = try {
                    current.workspace.activateExecutionEnvironment()
                    val produced = current.flow.run(input)
                    if (terminalClaimed.get() &&
                        produced.runCapability == current.reportContext.runCapability
                    ) {
                        produced
                    } else {
                        completeWorkerFailure(input, current, terminalClaimed, terminalCancelled)
                    }
                } catch (problem: Throwable) {
                    completeWorkerFailure(input, current, terminalClaimed, terminalCancelled)
                }
                source.finishInput()
                if (collector.await() is LogConnectorEnd.Incomplete) droppedLogs.incrementAndGet()
            }
        } catch (problem: Throwable) {
            if (report == null) {
                val cancelled = if (terminalClaimed.get()) {
                    terminalCancelled.get()
                } else {
                    synchronized(lock) {
                        lifecycle.claimTerminal(current.identity) ?: cancellation.get()
                    }
                }
                report = internalFailureReport(current.reportContext.runCapability, cancelled)
            }
        } finally {
            source.close()
            finishWorker(current, report)
        }
    }

    /** Converts a rare worker-launch failure into the same stable terminal failure branch. */
    private fun completeLaunchFailure(current: RunHostPreparedCandidate) {
        val cancelled = synchronized(lock) { lifecycle.claimTerminal(current.identity) } ?: false
        finishWorker(current, internalFailureReport(current.reportContext.runCapability, cancelled))
    }

    /** Commits one worker terminal result, signals quiescence, then calls the captured sink. */
    private fun finishWorker(current: RunHostPreparedCandidate, report: RunReport?) {
        var sink: RunHostEventSink? = null
        var terminal: RunHostSnapshotModel? = null
        var completion: CompletableFuture<Unit>? = null
        var ownerFinished = false
        try {
            val executionReleased = releaseExecution(current.executionLease)
            val resolvedReport = if (executionReleased) {
                report ?: internalFailureReport(
                    current.reportContext.runCapability,
                    cancellation.get(),
                )
            } else {
                internalFailureReport(current.reportContext.runCapability, cancellation.get())
            }
            val resolvedSnapshot = runCatching {
                terminalSnapshot(current.identity, resolvedReport)
            }.getOrElse {
                terminalSnapshot(
                    current.identity,
                    internalFailureReport(current.reportContext.runCapability, cancellation.get()),
                )
            }
            completion = synchronized(lock) {
                var lockedCompletion: CompletableFuture<Unit>? = null
                try {
                    terminal = resolvedSnapshot
                    snapshot = resolvedSnapshot
                    sink = eventSink
                } finally {
                    ownerFinished = true
                    lockedCompletion = finishInFlightLocked()
                }
                lockedCompletion
            }
        } finally {
            if (!ownerFinished) {
                completion = synchronized(lock) {
                    ownerFinished = true
                    finishInFlightLocked()
                }
            }
            completion?.complete(Unit)
        }
        if (terminal != null && sink != null) publishTerminal(sink, checkNotNull(terminal))
    }

    /** Validates, sequences, and forwards one log draft through the captured sink. */
    private fun publishLog(identity: RunHostIdentity, draft: LogDraft) {
        if (!draft.isValidRunHostLogDraft()) {
            droppedLogs.incrementAndGet()
            return
        }
        val sequence = reserveRunHostLogSequence(logSequence)
        if (sequence == null) {
            droppedLogs.incrementAndGet()
            return
        }
        val sink = synchronized(lock) { eventSink }
        if (sink == null) {
            droppedLogs.incrementAndGet()
            return
        }
        if (!runCatching { sink.publishLog(identity, sequence, draft) }.getOrDefault(false)) {
            droppedLogs.incrementAndGet()
            synchronized(lock) { if (eventSink === sink) eventSink = null }
        }
    }

    /** Delivers a terminal callback without changing already committed terminal facts. */
    private fun publishTerminal(sink: RunHostEventSink?, terminal: RunHostSnapshotModel) {
        if (sink == null || !runCatching { sink.publishTerminal(terminal) }.getOrDefault(false)) {
            synchronized(lock) { if (eventSink === sink) eventSink = null }
        }
    }

    /** Starts one external physical owner and creates the sole completion object for a new epoch. */
    private fun beginInFlightLocked() {
        check(Thread.holdsLock(lock)) { "in-flight registration requires the controller mutex" }
        if (inFlightPhysicalOperations == 0) {
            check(currentEpochCompletion == null) { "quiescent controller retained an epoch completion" }
            currentEpochCompletion = CompletableFuture()
        }
        inFlightPhysicalOperations += 1
    }

    /** Ends one physical owner and returns the completion object only on the 1-to-0 transition. */
    private fun finishInFlightLocked(): CompletableFuture<Unit>? {
        check(Thread.holdsLock(lock)) { "in-flight completion requires the controller mutex" }
        check(inFlightPhysicalOperations > 0) { "in-flight completion underflow" }
        inFlightPhysicalOperations -= 1
        if (inFlightPhysicalOperations != 0) return null
        return checkNotNull(currentEpochCompletion).also { currentEpochCompletion = null }
    }

    /** Ends one physical owner and signals waiters after releasing the controller mutex. */
    private fun finishInFlight() {
        val completion = synchronized(lock) { finishInFlightLocked() }
        completion?.complete(Unit)
    }

    /** Converts a Flow/activation contract failure while preserving an already-won terminal claim. */
    private fun completeWorkerFailure(
        input: BackendFlowInput,
        current: RunHostPreparedCandidate,
        terminalClaimed: AtomicBoolean,
        terminalCancelled: AtomicBoolean,
    ): RunReport {
        if (terminalClaimed.get()) {
            return internalFailureReport(current.reportContext.runCapability, terminalCancelled.get())
        }
        return runCatching { input.completeFailure() }.getOrElse {
            internalFailureReport(
                current.reportContext.runCapability,
                if (terminalClaimed.get()) terminalCancelled.get() else cancellation.get(),
            )
        }
    }

    /** Ends one execution owner; false retains the workspace rather than deleting beneath a live owner. */
    private fun releaseExecution(executionLease: RunHostExecutionLease): Boolean =
        runCatching {
            executionLease.close()
            true
        }.getOrDefault(false)

    /** Ends one candidate in the only legal order: execution first, then optional workspace deletion. */
    private fun releaseCandidate(candidate: RunHostPreparedCandidate, deleteWorkspace: Boolean) {
        if (!releaseExecution(candidate.executionLease) || !deleteWorkspace) return
        runCatching { candidate.workspace.deleteAfterTerminal() }
    }

    /** Checks candidate identity and capability without creating a second capability authority. */
    private fun candidateMatches(candidate: RunHostPreparedCandidate, identity: RunHostIdentity): Boolean =
        candidate.identity == identity &&
            candidate.rootScript.runId == identity.runId &&
            candidate.rootScript.generation == identity.generation &&
            candidate.reportContext.runCapability != RunCapability.UNRESOLVED

    /** Returns successful PREPARED facts using only the candidate report context as capability authority. */
    private fun preparedSnapshot(current: RunHostPreparedCandidate): RunHostSnapshotModel {
        val capability = current.reportContext.runCapability
        return RunHostSnapshotModel(
            identity = current.identity,
            state = RunHostState.PREPARED,
            lastCode = RunHostCode.OK,
            runCapability = capability,
            diagnosticOutcome = if (capability == RunCapability.DIAGNOSTIC_ONLY) {
                DiagnosticOutcome.NOT_RUN
            } else {
                DiagnosticOutcome.NOT_APPLICABLE
            },
            backendPrimitive = StageResult.NOT_RUN,
            scriptDispatch = StageResult.NOT_RUN,
            payloadItems = emptyList(),
            payloadOverall = StageResult.SKIPPED,
            finishItems = emptyList(),
            finishOverall = StageResult.SKIPPED,
            backendCleanup = StageResult.NOT_RUN,
            terminal = RunTerminal.NONE,
            logSequence = 0L,
            droppedLogs = 0L,
        )
    }

    /** Returns pre-prepare callback reservation facts. */
    private fun unresolvedSnapshot(identity: RunHostIdentity): RunHostSnapshotModel = RunHostSnapshotModel(
        identity = identity,
        state = lifecycle.state(),
        lastCode = RunHostCode.OK,
        runCapability = RunCapability.UNRESOLVED,
        diagnosticOutcome = DiagnosticOutcome.NOT_RUN,
        backendPrimitive = StageResult.NOT_RUN,
        scriptDispatch = StageResult.NOT_RUN,
        payloadItems = emptyList(),
        payloadOverall = StageResult.NOT_RUN,
        finishItems = emptyList(),
        finishOverall = StageResult.NOT_RUN,
        backendCleanup = StageResult.NOT_RUN,
        terminal = RunTerminal.NONE,
        logSequence = logSequence.get(),
        droppedLogs = droppedLogs.get(),
    )

    /** Returns terminal cancellation facts for a not-yet-running transaction. */
    private fun cancelledSnapshot(
        identity: RunHostIdentity,
        current: RunHostPreparedCandidate?,
        before: RunHostState,
    ): RunHostSnapshotModel {
        val capability = current?.reportContext?.runCapability ?: RunCapability.UNRESOLVED
        val outcome = when (capability) {
            RunCapability.UNRESOLVED -> DiagnosticOutcome.NOT_RUN
            RunCapability.PRODUCT_ROOT -> DiagnosticOutcome.NOT_APPLICABLE
            RunCapability.DIAGNOSTIC_ONLY -> if (before == RunHostState.RUNNING) {
                DiagnosticOutcome.UNKNOWN
            } else {
                DiagnosticOutcome.NOT_RUN
            }
        }
        return RunHostSnapshotModel(
            identity = identity,
            state = RunHostState.TERMINAL,
            lastCode = RunHostCode.OK,
            runCapability = capability,
            diagnosticOutcome = outcome,
            backendPrimitive = StageResult(StageState.CANCELLED, ResultReason.CANCELLED, ExitKind.NONE, 0),
            scriptDispatch = StageResult.SKIPPED,
            payloadItems = emptyList(),
            payloadOverall = StageResult.SKIPPED,
            finishItems = emptyList(),
            finishOverall = StageResult.SKIPPED,
            backendCleanup = StageResult.NOT_RUN,
            terminal = RunTerminal.CANCELLED,
            logSequence = logSequence.get(),
            droppedLogs = droppedLogs.get(),
        )
    }

    /** Converts one final report into the immutable RunHost terminal snapshot. */
    private fun terminalSnapshot(identity: RunHostIdentity, report: RunReport): RunHostSnapshotModel =
        RunHostSnapshotModel(
            identity = identity,
            state = RunHostState.TERMINAL,
            lastCode = RunHostCode.OK,
            runCapability = report.runCapability,
            diagnosticOutcome = report.diagnosticOutcome,
            backendPrimitive = report.backendPrimitive,
            scriptDispatch = report.rootScriptDispatch,
            payloadItems = report.payloadItems,
            payloadOverall = report.payloadOverall,
            finishItems = report.finishItems,
            finishOverall = report.finishOverall,
            backendCleanup = report.backendCleanup,
            terminal = report.terminal,
            logSequence = logSequence.get(),
            droppedLogs = droppedLogs.get(),
        )

    /** Produces a valid typed failure report when coroutine launch itself cannot start the worker. */
    private fun internalFailureReport(capability: RunCapability, cancelled: Boolean): RunReport {
        val unknown = StageResult(
            StageState.UNKNOWN,
            ResultReason.INTERNAL_CONTRACT_FAILURE,
            ExitKind.NONE,
            0,
        )
        return when (capability) {
            RunCapability.DIAGNOSTIC_ONLY -> RunReport(
                runCapability = capability,
                diagnosticOutcome = DiagnosticOutcome.UNKNOWN,
                backendPrimitive = unknown,
                rootScriptDispatch = StageResult.SKIPPED,
                payloadItems = emptyList(),
                payloadOverall = StageResult.SKIPPED,
                finishItems = emptyList(),
                finishOverall = StageResult.SKIPPED,
                backendCleanup = unknown,
                terminal = if (cancelled) RunTerminal.CANCELLED else RunTerminal.UNKNOWN,
            )

            RunCapability.PRODUCT_ROOT -> RunReport(
                runCapability = capability,
                diagnosticOutcome = DiagnosticOutcome.NOT_APPLICABLE,
                backendPrimitive = unknown,
                rootScriptDispatch = StageResult.SKIPPED,
                payloadItems = emptyList(),
                payloadOverall = unknown,
                finishItems = emptyList(),
                finishOverall = unknown,
                backendCleanup = unknown,
                terminal = if (cancelled) RunTerminal.CANCELLED else RunTerminal.UNKNOWN,
            )

            RunCapability.UNRESOLVED -> error("prepared candidate capability is unresolved")
        }
    }

    /** Applies a synchronous code to any already materialized snapshot under the controller mutex. */
    private fun updateLastCodeLocked(code: RunHostCode) {
        check(Thread.holdsLock(lock)) { "snapshot update requires the controller mutex" }
        snapshot = snapshot?.copy(lastCode = code, state = lifecycle.state())
    }
}
