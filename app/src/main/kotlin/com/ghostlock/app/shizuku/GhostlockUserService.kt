package com.ghostlock.app.shizuku

import android.content.Context
import android.os.SystemClock
import androidx.annotation.Keep
import com.ghostlock.app.backend.declarations.BackendPlugin
import com.ghostlock.app.backend.registry.BackendRegistry
import com.ghostlock.app.backend.transport.RunHostCode
import com.ghostlock.app.backend.transport.RunHostIdentity
import com.ghostlock.app.backend.transport.RunHostExecutionDomainVerifier
import com.ghostlock.app.backend.transport.RunHostRequestModel
import com.ghostlock.app.backend.transport.RunHostRequestValidation
import com.ghostlock.app.backend.transport.RunHostRequestValidator
import com.ghostlock.app.backend.transport.RunHostProductPipeline
import com.ghostlock.app.backend.transport.PreparedProductRun
import com.ghostlock.app.backend.transport.RunHostSnapshotModel
import com.ghostlock.app.backend.transport.RunHostState
import com.ghostlock.app.backend.transport.RunHostStateMachine
import com.ghostlock.app.backend.transport.RunHostRole
import com.ghostlock.app.backend.transport.duplicateDescriptors
import com.ghostlock.app.backend.transport.isValidRunHostLogDraft
import com.ghostlock.app.backend.transport.parcelDataSize
import com.ghostlock.app.backend.transport.reserveRunHostLogSequence
import com.ghostlock.app.backend.transport.semanticFingerprint
import com.ghostlock.app.backend.transport.toBoundedWire
import com.ghostlock.app.backend.transport.toModel
import com.ghostlock.app.backend.transport.requiresWorkspaceRetention
import com.ghostlock.app.core.flow.BackendFlowInput
import com.ghostlock.app.core.flow.Flow
import com.ghostlock.app.core.flow.FlowReportContext
import com.ghostlock.app.core.flow.FlowTerminalClaim
import com.ghostlock.app.core.flow.completeFailure
import com.ghostlock.app.data.run.AndroidRunWorkspace
import com.ghostlock.app.data.run.RootArtifactStagingAdapter
import com.ghostlock.app.domain.backend.AbortPolicy
import com.ghostlock.app.domain.backend.BackendAvailability
import com.ghostlock.app.domain.run.DiagnosticOutcome
import com.ghostlock.app.domain.run.ExitKind
import com.ghostlock.app.domain.run.ProfileSnapshot
import com.ghostlock.app.domain.run.ResultReason
import com.ghostlock.app.domain.run.RunCancellationView
import com.ghostlock.app.domain.run.RunCapability
import com.ghostlock.app.domain.run.RunLogDraft
import com.ghostlock.app.domain.run.RunTerminal
import com.ghostlock.app.domain.run.StageResult
import com.ghostlock.app.domain.run.StageState
import com.ghostlock.app.run.IRunHost
import com.ghostlock.app.run.IRunHostCallback
import com.ghostlock.app.run.RunHostLogRecord
import com.ghostlock.app.run.RunHostRequest
import com.ghostlock.app.run.RunHostSnapshot
import com.ghostlock.app.run.RunHostSnapshotReply
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicLong
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch

/**
 * Shizuku UserService implementation of the exact typed IRunHost business schema.
 *
 * API-v13+ supplies a package Context through the kept constructor. Older default-constructor launches remain
 * fail-closed at prepare because staging cannot be proven without a host-owned context. Framework destroy only
 * exits after typed requestClose has moved the one-shot host to DESTROYED.
 */
class GhostlockUserService : IRunHost.Stub {
    @Volatile
    private var context: Context? = null
    private val lock = Any()
    private val lifecycle = RunHostStateMachine()
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
    private val staging = RootArtifactStagingAdapter()
    private val productPipeline = RunHostProductPipeline(staging)
    private val cancellation = AtomicBoolean(false)
    private val logSequence = AtomicLong(0L)
    private val droppedLogs = AtomicLong(0L)
    private var callback: IRunHostCallback? = null
    private var prepared: PreparedRun? = null
    private var snapshot: RunHostSnapshotModel? = null

    /** Default constructor required by older Shizuku UserService loaders. */
    constructor()

    /** Context constructor preferred by current Shizuku UserService loaders. */
    @Keep
    constructor(context: Context) {
        this.context = context
    }

    /** Reserves or replaces a callback for the exact run identity. */
    override fun setCallback(runId: String, generation: Long, callback: IRunHostCallback): Int {
        val identity = identityOrNull(runId, generation) ?: return RunHostCode.INVALID_REQUEST.wireValue
        val code = lifecycle.setCallback(identity)
        if (code == RunHostCode.OK) synchronized(lock) { this.callback = callback }
        return code.wireValue
    }

    /** Validates, stages, and commits one shell-domain prepare. */
    override fun prepare(request: RunHostRequest): Int = prepareInternal(request).wireValue

    /** Starts the prepared one-shot Flow. */
    override fun start(runId: String, generation: Long): Int {
        val identity = identityOrNull(runId, generation) ?: return RunHostCode.INVALID_REQUEST.wireValue
        val code = lifecycle.start(identity)
        if (code == RunHostCode.OK) {
            val current = synchronized(lock) { prepared }
                ?: return RunHostCode.INTERNAL_CONTRACT_FAILURE.wireValue
            scope.launch { runPrepared(current) }
        }
        updateLastCode(code)
        return code.wireValue
    }

    /** Returns the current immutable snapshot reply. */
    override fun getSnapshot(runId: String, generation: Long): RunHostSnapshotReply {
        val identity = identityOrNull(runId, generation)
            ?: return reply(RunHostCode.INVALID_REQUEST, null)
        val code = lifecycle.snapshotCode(identity)
        val model = if (code == RunHostCode.OK) synchronized(lock) {
            snapshot ?: unresolvedSnapshot(identity)
        } else {
            null
        }
        return reply(code, model)
    }

    /** Requests policy-gated cancellation without killing the UserService. */
    override fun cancel(runId: String, generation: Long): Int {
        val identity = identityOrNull(runId, generation) ?: return RunHostCode.INVALID_REQUEST.wireValue
        val before = lifecycle.state()
        val current = synchronized(lock) { prepared }
        val safe = current?.plugin?.spec?.abortPolicy == AbortPolicy.SAFE_DURING_RUN
        val code = lifecycle.cancel(identity, safe)
        if (code == RunHostCode.OK) {
            cancellation.set(true)
            if (before == RunHostState.RUNNING) {
                runCatching { current?.flow?.stop() }
            } else {
                val terminalSnapshot = cancelledSnapshot(identity, current, before)
                synchronized(lock) { snapshot = terminalSnapshot }
                publishTerminalCallback(terminalSnapshot)
            }
        }
        updateLastCode(code)
        return code.wireValue
    }

    /** Authorizes later framework destroy only from NEW/PREPARED/TERMINAL. */
    override fun requestClose(runId: String, generation: Long): Int {
        val identity = identityOrNull(runId, generation) ?: return RunHostCode.INVALID_REQUEST.wireValue
        val code = lifecycle.requestClose(identity)
        if (code == RunHostCode.OK) {
            synchronized(lock) {
                if (snapshot?.terminal?.requiresWorkspaceRetention() != true) {
                    prepared?.workspace?.deleteAfterTerminal()
                }
                prepared = null
                callback = null
                snapshot = snapshot?.copy(state = RunHostState.DESTROYED, lastCode = code)
            }
        }
        updateLastCode(code)
        return code.wireValue
    }

    /** Shizuku fixed destroy transaction; unauthorized calls are no-ops. */
    override fun destroy() {
        if (!lifecycle.frameworkDestroyAuthorized()) return
        synchronized(lock) {
            if (snapshot?.terminal?.requiresWorkspaceRetention() != true) {
                prepared?.workspace?.deleteAfterTerminal()
            }
        }
        System.exit(0)
    }

    /** Implements deterministic prepare ordering for the shell execution domain. */
    private fun prepareInternal(wire: RunHostRequest): RunHostCode {
        val parcelBytes = runCatching { wire.parcelDataSize() }.getOrElse {
            return RunHostCode.INVALID_REQUEST
        }
        if (parcelBytes > com.ghostlock.app.backend.transport.RunHostProtocol.MAX_REQUEST_PARCEL_BYTES) {
            return RunHostCode.LIMIT_EXCEEDED
        }
        val model = runCatching { wire.toModel() }.getOrElse { return RunHostCode.INVALID_REQUEST }
        when (val structural = RunHostRequestValidator.validateStructure(model)) {
            RunHostRequestValidation.Valid -> Unit
            is RunHostRequestValidation.Invalid -> return structural.code
        }
        val identity = identityOrNull(model.runId, model.generation) ?: return RunHostCode.INVALID_REQUEST
        val fingerprint = model.semanticFingerprint()
        val preflight = lifecycle.preparePreflight(identity, fingerprint)
        if (preflight != RunHostCode.OK) return preflight
        val plugin = BackendRegistry.plugin(model.backendKey) ?: return RunHostCode.UNKNOWN_BACKEND
        when (val shape = RunHostRequestValidator.validateCapability(model, plugin.spec.runCapability)) {
            RunHostRequestValidation.Valid -> Unit
            is RunHostRequestValidation.Invalid -> return shape.code
        }
        if (!RunHostExecutionDomainVerifier.currentProcessMatches(
                expected = plugin.spec.executionDomain,
                role = RunHostRole.SHIZUKU_SHELL,
                applicationUid = context?.applicationInfo?.uid ?: -1,
            )
        ) {
            return RunHostCode.EXECUTION_DOMAIN_MISMATCH
        }
        val hostContext = context ?: return RunHostCode.NOT_READY
        if (plugin.availability() is BackendAvailability.NotReady) return RunHostCode.NOT_READY
        val ownedDescriptors = runCatching { wire.duplicateDescriptors(model) }
            .getOrElse { return RunHostCode.ARTIFACT_INVALID }
        var workspace: AndroidRunWorkspace? = null
        return try {
            val createdWorkspace = AndroidRunWorkspace.createShizukuShell(
                packageName = hostContext.packageName,
                identity = identity,
                backendKey = plugin.spec.key,
            )
            workspace = createdWorkspace
            val staged = staging.stage(createdWorkspace, ownedDescriptors)
            val product = when (plugin.spec.runCapability) {
                RunCapability.DIAGNOSTIC_ONLY -> null
                RunCapability.PRODUCT_ROOT -> productPipeline.prepare(
                    workspace = createdWorkspace,
                    identity = identity,
                    request = model,
                    staged = staged,
                )

                RunCapability.UNRESOLVED -> {
                    createdWorkspace.deleteAfterTerminal()
                    return RunHostCode.INTERNAL_CONTRACT_FAILURE
                }
            }
            val artifactCatalog = product?.artifacts ?: staging.publishCatalog(staged)
            val profileArtifact = requireNotNull(artifactCatalog[model.profileArtifactId]) {
                "staged profile artifact is missing"
            }
            val rootArtifact = product?.rootScript
                ?: staging.publishDiagnosticArtifact(createdWorkspace, identity)
            val profile = product?.profile ?: ProfileSnapshot(
                artifactId = model.profileArtifactId,
                schemaVersion = ProfileSnapshot.CURRENT_SCHEMA_VERSION,
                sha256 = profileArtifact.identity.sha256,
            )
            val code = lifecycle.prepare(identity, fingerprint)
            if (code != RunHostCode.OK && code != RunHostCode.ALREADY_APPLIED) {
                createdWorkspace.deleteAfterTerminal()
                return code
            }
            if (code == RunHostCode.OK) {
                val current = PreparedRun(
                    identity = identity,
                    plugin = plugin,
                    flow = plugin.createFlow(),
                    workspace = createdWorkspace,
                    rootScript = rootArtifact,
                    profile = profile,
                    artifacts = artifactCatalog,
                    persistentStateDirectory = createdWorkspace.persistentStateDirectory.absolutePath,
                    product = product,
                )
                synchronized(lock) {
                    prepared = current
                    snapshot = preparedSnapshot(current)
                }
            } else {
                createdWorkspace.deleteAfterTerminal()
            }
            code
        } catch (problem: IllegalArgumentException) {
            workspace?.deleteAfterTerminal()
            RunHostCode.ARTIFACT_INVALID
        } catch (problem: Throwable) {
            workspace?.deleteAfterTerminal()
            RunHostCode.IO_FAILURE
        }
    }

    /** Runs one Flow and publishes its immutable terminal snapshot. */
    private suspend fun runPrepared(current: PreparedRun) {
        val input = BackendFlowInput(
            runId = current.identity.runId,
            generation = current.identity.generation,
            rootScript = current.rootScript,
            profile = current.profile,
            logs = { draft -> publishLog(current.identity, draft) },
            cancellation = RunCancellationView { cancellation.get() },
            reportContext = FlowReportContext(
                runCapability = current.plugin.spec.runCapability,
                productPlan = current.product?.plan,
            ),
            terminalClaim = FlowTerminalClaim { lifecycle.claimTerminal(current.identity) },
            artifacts = current.artifacts,
            persistentStateDirectory = current.persistentStateDirectory,
        )
        val report = runCatching { current.flow.run(input) }.getOrElse {
            runCatching { input.completeFailure() }.getOrNull() ?: return
        }
        val terminalSnapshot = RunHostSnapshotModel(
            identity = current.identity,
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
        synchronized(lock) { snapshot = terminalSnapshot }
        publishTerminalCallback(terminalSnapshot)
    }

    /** Stamps one bounded log draft and forwards it to the current callback. */
    private fun publishLog(identity: RunHostIdentity, draft: RunLogDraft) {
        if (!draft.isValidRunHostLogDraft()) {
            droppedLogs.incrementAndGet()
            return
        }
        val sequence = reserveRunHostLogSequence(logSequence)
        if (sequence == null) {
            droppedLogs.incrementAndGet()
            return
        }
        val record = RunHostLogRecord().also { parcel ->
            parcel.runId = identity.runId
            parcel.generation = identity.generation
            parcel.sequence = sequence
            parcel.elapsedRealtimeMs = SystemClock.elapsedRealtime()
            parcel.origin = draft.origin.wireValue
            parcel.level = draft.level.wireValue
            parcel.message = draft.message
        }
        if (runCatching { record.parcelDataSize() }.getOrDefault(Int.MAX_VALUE) >
            com.ghostlock.app.backend.transport.RunHostProtocol.MAX_LOG_PARCEL_BYTES
        ) {
            droppedLogs.incrementAndGet()
            return
        }
        val target = synchronized(lock) { callback }
        if (target == null) {
            droppedLogs.incrementAndGet()
            return
        }
        runCatching { target.onLog(record) }.onFailure {
            droppedLogs.incrementAndGet()
            synchronized(lock) { if (callback === target) callback = null }
        }
    }

    /** Delivers the terminal snapshot at most once to the current callback. */
    private fun publishTerminalCallback(model: RunHostSnapshotModel) {
        val target = synchronized(lock) { callback }
        runCatching { target?.onTerminal(model.toBoundedWire()) }.onFailure {
            synchronized(lock) { if (callback === target) callback = null }
        }
    }

    /** Returns successful PREPARED facts. */
    private fun preparedSnapshot(current: PreparedRun): RunHostSnapshotModel = RunHostSnapshotModel(
        identity = current.identity,
        state = RunHostState.PREPARED,
        lastCode = RunHostCode.OK,
        runCapability = current.plugin.spec.runCapability,
        diagnosticOutcome = if (current.plugin.spec.runCapability == RunCapability.DIAGNOSTIC_ONLY) {
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

    /** Returns pre-prepare identity reservation facts. */
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

    /** Returns typed cancellation facts. */
    private fun cancelledSnapshot(
        identity: RunHostIdentity,
        current: PreparedRun?,
        before: RunHostState,
    ): RunHostSnapshotModel {
        val capability = current?.plugin?.spec?.runCapability ?: RunCapability.UNRESOLVED
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

    /** Returns one generated snapshot reply. */
    private fun reply(code: RunHostCode, model: RunHostSnapshotModel?): RunHostSnapshotReply =
        RunHostSnapshotReply().also { parcel ->
            parcel.code = code.wireValue
            parcel.snapshot = model?.toBoundedWire()
        }

    /** Updates the current snapshot diagnostic code. */
    private fun updateLastCode(code: RunHostCode) {
        synchronized(lock) { snapshot = snapshot?.copy(lastCode = code, state = lifecycle.state()) }
    }

    /** Parses a compound identity without throwing through Binder. */
    private fun identityOrNull(runId: String, generation: Long): RunHostIdentity? =
        runCatching { RunHostIdentity(runId, generation) }.getOrNull()

    /** Fully owned prepared run. */
    private data class PreparedRun(
        val identity: RunHostIdentity,
        val plugin: BackendPlugin,
        val flow: Flow,
        val workspace: AndroidRunWorkspace,
        val rootScript: com.ghostlock.app.core.rootscript.RootScriptArtifact,
        val profile: ProfileSnapshot,
        val artifacts: Map<String, com.ghostlock.app.core.flow.PublishedFlowArtifact>,
        val persistentStateDirectory: String,
        val product: PreparedProductRun?,
    )
}
