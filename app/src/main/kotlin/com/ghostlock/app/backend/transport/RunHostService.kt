package com.ghostlock.app.backend.transport

import android.app.Service
import android.content.Intent
import android.os.IBinder
import android.os.ParcelFileDescriptor
import android.os.SystemClock
import android.system.Os
import com.ghostlock.app.backend.declarations.BackendPlugin
import com.ghostlock.app.backend.registry.BackendRegistry
import com.ghostlock.app.core.flow.BackendFlowInput
import com.ghostlock.app.core.flow.Flow
import com.ghostlock.app.core.flow.FlowReportContext
import com.ghostlock.app.core.flow.FlowTerminalClaim
import com.ghostlock.app.core.flow.completeFailure
import com.ghostlock.app.data.run.AndroidRunWorkspace
import com.ghostlock.app.data.run.OpenedRunInputArtifact
import com.ghostlock.app.data.run.RootArtifactStagingAdapter
import com.ghostlock.app.domain.backend.AbortPolicy
import com.ghostlock.app.domain.backend.BackendAvailability
import com.ghostlock.app.domain.run.DiagnosticOutcome
import com.ghostlock.app.domain.run.ExitKind
import com.ghostlock.app.domain.run.FinishItemResult
import com.ghostlock.app.domain.run.ItemState
import com.ghostlock.app.domain.run.PayloadItemResult
import com.ghostlock.app.domain.run.ProfileSnapshot
import com.ghostlock.app.domain.run.ResultReason
import com.ghostlock.app.domain.run.RunCancellationView
import com.ghostlock.app.domain.run.RunCapability
import com.ghostlock.app.domain.run.LogDraft
import com.ghostlock.app.domain.run.RunTerminal
import com.ghostlock.app.domain.run.StageResult
import com.ghostlock.app.domain.run.StageState
import com.ghostlock.app.run.FinishItemParcel
import com.ghostlock.app.run.IRunHost
import com.ghostlock.app.run.IRunHostCallback
import com.ghostlock.app.run.ItemResultParcel
import com.ghostlock.app.run.PayloadItemParcel
import com.ghostlock.app.run.RunHostLogRecord
import com.ghostlock.app.run.RunHostRequest
import com.ghostlock.app.run.RunHostSnapshot
import com.ghostlock.app.run.RunHostSnapshotReply
import com.ghostlock.app.run.RunInputArtifactParcel
import com.ghostlock.app.run.StageResultParcel
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicLong
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch

/**
 * App-process-domain implementation of the one-shot typed RunHost AIDL contract.
 *
 * The service validates bounded request metadata, resolves an immutable plugin key, stages owned descriptor
 * copies, publishes the diagnostic artifact or product root script, creates one Flow, and publishes one immutable
 * terminal snapshot. Logs and callbacks are diagnostic only; snapshot facts remain authoritative.
 */
class RunHostService : Service() {
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

    /** Typed binder exported only to the non-exported service binding. */
    private val binder = object : IRunHost.Stub() {
        override fun setCallback(
            runId: String,
            generation: Long,
            callback: IRunHostCallback,
        ): Int {
            val identity = identityOrNull(runId, generation) ?: return RunHostCode.INVALID_REQUEST.wireValue
            val code = lifecycle.setCallback(identity)
            if (code == RunHostCode.OK) synchronized(lock) { this@RunHostService.callback = callback }
            return code.wireValue
        }

        override fun prepare(request: RunHostRequest): Int = prepareInternal(request).wireValue

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

        override fun getSnapshot(runId: String, generation: Long): RunHostSnapshotReply {
            val identity = identityOrNull(runId, generation)
            if (identity == null) return reply(RunHostCode.INVALID_REQUEST, null)
            val code = lifecycle.snapshotCode(identity)
            val model = if (code == RunHostCode.OK) synchronized(lock) {
                snapshot ?: unresolvedSnapshot(identity)
            } else {
                null
            }
            return reply(code, model)
        }

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

        override fun destroy() {
            if (lifecycle.frameworkDestroyAuthorized()) stopSelf()
        }
    }

    /** Returns the typed RunHost binder. */
    override fun onBind(intent: Intent?): IBinder = binder

    /** Converts, validates, resolves, stages, and commits one successful prepare. */
    private fun prepareInternal(wire: RunHostRequest): RunHostCode {
        val parcelBytes = runCatching { wire.parcelDataSize() }.getOrElse {
            return RunHostCode.INVALID_REQUEST
        }
        if (parcelBytes > RunHostProtocol.MAX_REQUEST_PARCEL_BYTES) return RunHostCode.LIMIT_EXCEEDED
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
                role = RunHostRole.APP,
                applicationUid = applicationInfo.uid,
            )
        ) {
            return RunHostCode.EXECUTION_DOMAIN_MISMATCH
        }
        if (plugin.availability() is BackendAvailability.NotReady) return RunHostCode.NOT_READY
        val ownedDescriptors = runCatching { wire.duplicateDescriptors(model) }
            .getOrElse { return RunHostCode.ARTIFACT_INVALID }
        var workspace: AndroidRunWorkspace? = null
        return try {
            val createdWorkspace = AndroidRunWorkspace.createApp(
                context = this,
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
            val candidate = PreparedRun(
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
            val candidateSnapshot = preparedSnapshot(candidate, RunHostCode.OK)
            val code = synchronized(lock) {
                lifecycle.prepare(identity, fingerprint).also { committed ->
                    if (committed == RunHostCode.OK) {
                        prepared = candidate
                        snapshot = candidateSnapshot
                    }
                }
            }
            if (code != RunHostCode.OK) createdWorkspace.deleteAfterTerminal()
            code
        } catch (problem: IllegalArgumentException) {
            workspace?.deleteAfterTerminal()
            RunHostCode.ARTIFACT_INVALID
        } catch (problem: Throwable) {
            workspace?.deleteAfterTerminal()
            RunHostCode.IO_FAILURE
        }
    }

    /** Runs the prepared Flow and publishes exactly one terminal snapshot. */
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
        val report = runCatching {
            Os.setenv("GHOSTLOCK_HOME", current.workspace.root.absolutePath, true)
            current.flow.run(input)
        }.getOrElse {
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

    /** Stamps and delivers one bounded log draft. */
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
            RunHostProtocol.MAX_LOG_PARCEL_BYTES
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

    /** Delivers a terminal snapshot at most once to the currently registered callback. */
    private fun publishTerminalCallback(model: RunHostSnapshotModel) {
        val target = synchronized(lock) { callback }
        runCatching { target?.onTerminal(model.toBoundedWire()) }.onFailure {
            synchronized(lock) { if (callback === target) callback = null }
        }
    }

    /** Updates the current snapshot's diagnostic code without changing terminal facts. */
    private fun updateLastCode(code: RunHostCode) {
        synchronized(lock) { snapshot = snapshot?.copy(lastCode = code, state = lifecycle.state()) }
    }

    /** Returns the successful prepare snapshot. */
    private fun preparedSnapshot(current: PreparedRun, code: RunHostCode): RunHostSnapshotModel =
        RunHostSnapshotModel(
            identity = current.identity,
            state = RunHostState.PREPARED,
            lastCode = code,
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

    /** Returns a NEW unresolved snapshot after callback identity reservation. */
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

    /** Returns the immutable cancellation snapshot for the state at which cancellation linearized. */
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

    /** Creates a stable snapshot reply wire object. */
    private fun reply(code: RunHostCode, model: RunHostSnapshotModel?): RunHostSnapshotReply =
        RunHostSnapshotReply().also { parcel ->
            parcel.code = code.wireValue
            parcel.snapshot = model?.toBoundedWire()
        }

    /** Parses a compound identity without throwing through Binder. */
    private fun identityOrNull(runId: String, generation: Long): RunHostIdentity? =
        runCatching { RunHostIdentity(runId, generation) }.getOrNull()

    /** One fully owned prepared run. */
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

/** Converts a decoded AIDL request into the normalized transport model. */
internal fun RunHostRequest.toModel(): RunHostRequestModel = RunHostRequestModel(
    runId = runId,
    generation = generation,
    backendKey = backendKey,
    profileArtifactId = profileArtifactId,
    payloadItems = payloadItems.map(PayloadItemParcel::toModel),
    finishItems = finishItems.map(FinishItemParcel::toModel),
    artifacts = artifacts.map(RunInputArtifactParcel::toModel),
)

/** Duplicates every request descriptor so RunHost owns its lifetime independently of Binder input objects. */
internal fun RunHostRequest.duplicateDescriptors(
    model: RunHostRequestModel,
): List<OpenedRunInputArtifact> {
    require(artifacts.size == model.artifacts.size) { "artifact count changed during decode" }
    val owned = mutableListOf<OpenedRunInputArtifact>()
    try {
        artifacts.zip(model.artifacts).forEach { (wire, metadata) ->
            val source = requireNotNull(wire.fd) { "artifact descriptor is null" }
            owned += OpenedRunInputArtifact(metadata, ParcelFileDescriptor.dup(source.fileDescriptor))
        }
        return owned
    } catch (problem: Throwable) {
        owned.forEach { artifact -> runCatching { artifact.descriptor.close() } }
        throw problem
    }
}

/** Decodes one artifact metadata record, rejecting unknown enum values. */
private fun RunInputArtifactParcel.toModel(): RunHostArtifactRequest = RunHostArtifactRequest(
    logicalId = logicalId,
    kind = RunHostArtifactKind.entries.firstOrNull { candidate -> candidate.wireValue == kind }
        ?: throw IllegalArgumentException("unknown artifact kind"),
    byteCount = byteCount,
    sha256 = sha256,
)

/** Decodes one Payload item, rejecting unknown kind/acceptance values. */
private fun PayloadItemParcel.toModel(): RunHostPayloadItemRequest = RunHostPayloadItemRequest(
    itemId = itemId,
    kind = RunHostPayloadKind.entries.firstOrNull { candidate -> candidate.wireValue == kind }
        ?: throw IllegalArgumentException("unknown Payload kind"),
    required = required,
    artifactId = artifactId,
    args = args.toList(),
    acceptance = RunHostPayloadAcceptance.entries.firstOrNull { candidate ->
        candidate.wireValue == acceptanceKind
    } ?: throw IllegalArgumentException("unknown Payload acceptance"),
)

/** Decodes one Finish item, rejecting unknown kind/severity values. */
private fun FinishItemParcel.toModel(): RunHostFinishItemRequest = RunHostFinishItemRequest(
    itemId = itemId,
    kind = RunHostFinishKind.entries.firstOrNull { candidate -> candidate.wireValue == kind }
        ?: throw IllegalArgumentException("unknown Finish kind"),
    severity = RunHostFinishSeverity.entries.firstOrNull { candidate -> candidate.wireValue == severity }
        ?: throw IllegalArgumentException("unknown Finish severity"),
    alwaysRun = alwaysRun,
    artifactId = artifactId,
    args = args.toList(),
)

/** Encodes an immutable transport snapshot to the generated AIDL Parcelable. */
internal fun RunHostSnapshotModel.toWire(): RunHostSnapshot = RunHostSnapshot().also { parcel ->
    parcel.runId = identity.runId
    parcel.generation = identity.generation
    parcel.state = state.wireValue
    parcel.lastCode = lastCode.wireValue
    parcel.runCapability = runCapability.wireValue
    parcel.diagnosticOutcome = diagnosticOutcome.wireValue
    parcel.backendPrimitive = backendPrimitive.toWire()
    parcel.scriptDispatch = scriptDispatch.toWire()
    parcel.payloadItems = payloadItems.map(PayloadItemResult::toWire)
    parcel.payloadOverall = payloadOverall.toWire()
    parcel.finishItems = finishItems.map(FinishItemResult::toWire)
    parcel.finishOverall = finishOverall.toWire()
    parcel.backendCleanup = backendCleanup.toWire()
    parcel.terminal = terminal.wireValue
    parcel.logSequence = logSequence
    parcel.droppedLogs = droppedLogs
}

/** Encodes one stable stage fact. */
private fun StageResult.toWire(): StageResultParcel = StageResultParcel().also { parcel ->
    parcel.state = state.wireValue
    parcel.reason = reason.wireValue
    parcel.exitKind = exitKind.wireValue
    parcel.exitValue = exitValue
}

/** Encodes one Payload item fact. */
private fun PayloadItemResult.toWire(): ItemResultParcel = ItemResultParcel().also { parcel ->
    parcel.itemId = itemId
    parcel.state = state.wireValue
    parcel.reason = reason.wireValue
    parcel.exitKind = exitKind.wireValue
    parcel.exitValue = exitValue
}

/** Encodes one Finish item fact. */
private fun FinishItemResult.toWire(): ItemResultParcel = ItemResultParcel().also { parcel ->
    parcel.itemId = itemId
    parcel.state = state.wireValue
    parcel.reason = reason.wireValue
    parcel.exitKind = exitKind.wireValue
    parcel.exitValue = exitValue
}
