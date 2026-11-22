package com.ghostlock.app.core.run

import com.ghostlock.app.core.log.LogHub
import com.ghostlock.app.composition.backend.BackendCatalogView
import com.ghostlock.app.composition.backend.ResolvedBackendProjection
import com.ghostlock.app.domain.backend.AbortPolicy
import com.ghostlock.app.domain.backend.BackendAvailability
import com.ghostlock.app.domain.backend.BackendAvailabilityReason
import com.ghostlock.app.domain.backend.BackendDescriptor
import com.ghostlock.app.domain.finish.FinishDefinition
import com.ghostlock.app.domain.finish.FinishRepository
import com.ghostlock.app.domain.payload.PayloadDefinition
import com.ghostlock.app.domain.payload.PayloadRepository
import com.ghostlock.app.domain.privilege.AttackExecutorState
import com.ghostlock.app.domain.privilege.ObservePrivilegeUseCase
import com.ghostlock.app.domain.run.LogFormatConfig
import com.ghostlock.app.domain.run.LogLevel
import com.ghostlock.app.domain.run.LogSinkRequest
import com.ghostlock.app.domain.run.RunCapability
import com.ghostlock.app.domain.run.RunCommand
import com.ghostlock.app.domain.run.RunPresentationPort
import com.ghostlock.app.domain.run.RunSessionPort
import com.ghostlock.app.domain.run.RunSessionState
import com.ghostlock.app.domain.settings.OperationalPreferences
import com.ghostlock.app.domain.settings.OperationalPreferencesPort
import com.ghostlock.app.domain.settings.TerminalLogLevel
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.Deferred
import kotlinx.coroutines.async
import kotlinx.coroutines.flow.StateFlow

/**
 * Application-scoped owner of admission, actor cutoffs, LogHub acceptance, and dispatch-lease transfer.
 *
 * @param catalogView One terminal READY catalog generation, or null before/after failed bootstrap.
 * @param availability Dynamic resource-free readiness resolver by stable backend key.
 * @param backendLabel Presentation-owned localized label resolver.
 * @param selectedKey Current selected stable backend key.
 * @param logHub Sole application log and run-slot authority.
 * @param dispatch Android freeze, request assembly, and fixed-target dispatch boundary.
 * @param sinkProvider Prebuilt unopened storage-sink provider.
 * @param presentation Shared Android presentation boundary for abort text and font resolution.
 * @param operationalPreferencesPort Actor-backed operational settings authority.
 * @param payloadRepository Actor-backed Payload authority.
 * @param finishRepository Actor-backed Finish authority.
 * @param applicationScope Application-lifetime coroutine scope.
 * @param observePrivilege Sole privilege state observer.
 */
class RunSessionCoordinator(
    private val catalogView: () -> BackendCatalogView?,
    private val availability: (String) -> BackendAvailability?,
    private val backendLabel: (String) -> String?,
    private val selectedKey: () -> String?,
    private val logHub: LogHub,
    private val dispatch: RunDispatchPort,
    private val sinkProvider: LogSinkProvider,
    private val presentation: RunPresentationPort,
    private val operationalPreferencesPort: OperationalPreferencesPort,
    private val payloadRepository: PayloadRepository,
    private val finishRepository: FinishRepository,
    private val applicationScope: CoroutineScope,
    observePrivilege: ObservePrivilegeUseCase,
) : RunSessionPort {
    private val operationalPreferences = operationalPreferencesPort.preferences
    private val privilege = observePrivilege()

    override val state: StateFlow<RunSessionState> = logHub.state

    /** Refreshes the idle projection from one current privilege and settings snapshot. */
    override fun refresh() {
        val spec = currentProjection()?.descriptor
        val executor = privilege.value.attackExecutor()
        logHub.updateContext(buildContext(spec, decision(spec, executor)))
    }

    /**
     * Captures admission, executor target, settings, Payload, and Finish before asynchronous preparation.
     *
     * @param command Exact UI-selected backend command.
     * @return Deferred acceptance result; true only after LogHub commits the run.
     */
    override fun start(command: RunCommand): Deferred<Boolean> {
        val readyView = catalogView()
        val selected = selectedKey()
        val projection = readyView?.resolve(selected)?.takeIf { candidate ->
            candidate.descriptor.selectable && candidate.descriptor.key == command.backendKey
        } ?: run {
            refresh()
            return CompletableDeferred(false)
        }
        val spec = projection.descriptor
        val executor = privilege.value.attackExecutor()
        val admission = decision(spec, executor)
        if (!admission.allowed) {
            logHub.updateContext(buildContext(spec, admission))
            return CompletableDeferred(false)
        }

        val preferenceCutoff = operationalPreferencesPort.snapshot()
        val inputCutoff = captureInputCutoff(projection)
        return applicationScope.async(start = CoroutineStart.UNDISPATCHED) {
            val preferences = runCatching { preferenceCutoff.await() }.getOrElse { problem ->
                logHub.updateContext(
                    buildContext(spec, admission).copy(
                        startAllowed = false,
                        blockedReasons = listOf(
                            "Operational settings snapshot failed: " + problem::class.java.simpleName,
                        ),
                    ),
                )
                return@async false
            }
            val context = buildContext(spec, admission, preferences)
            when (val prepared = dispatch.prepare(spec, executor.kind, inputCutoff)) {
                is RunDispatchPreparation.Failed -> {
                    val prefix = when (prepared.stage) {
                        RunDispatchFailureStage.FREEZE -> "Run input freeze failed: "
                        RunDispatchFailureStage.REQUEST_ASSEMBLY -> "Typed RunHost request assembly failed: "
                    }
                    logHub.updateContext(
                        context.copy(
                            startAllowed = false,
                            blockedReasons = listOf(prefix + prepared.causeTypeName),
                        ),
                    )
                    false
                }

                is RunDispatchPreparation.Prepared -> acceptPrepared(
                    lease = prepared.lease,
                    context = context,
                    preferences = preferences,
                )
            }
        }
    }

    /** Propagates explicit cancellation through the sole LogHub-owned active job. */
    override fun abort(): Boolean = logHub.requestAbort(presentation.abortDisabledMessage())

    /** Clears only retained UI rows. */
    override fun clearTerminal() = logHub.clearTerminal()

    /** Transfers one prepared lease only after LogHub's accepted commit. */
    private fun acceptPrepared(
        lease: RunDispatchLease,
        context: RunSessionState,
        preferences: OperationalPreferences,
    ): Boolean {
        val request = LogSinkRequest(
            enabled = preferences.autoExportLogs,
            runId = lease.identity.runId,
            generation = lease.identity.generation,
            backendKey = lease.backendKey,
            relativePath = preferences.logExportLocation,
            formatConfig = LogFormatConfig(showTimestamp = preferences.showLogTimestamp),
        )
        return try {
            val sinks = sinkProvider.create(request) { failure ->
                logHub.reportSinkFailure(request, failure)
            }
            val accepted = logHub.start(
                context = context,
                sinkRequest = request,
                storageSinks = sinks,
                onExecutionFinished = lease::close,
            ) { attach -> lease.dispatch(attach) }
            if (!accepted) lease.close()
            accepted
        } catch (problem: Throwable) {
            lease.close()
            throw problem
        }
    }

    /** Captures exact actor transactions synchronously before any host preparation begins. */
    private fun captureInputCutoff(projection: ResolvedBackendProjection): RunInputCutoff {
        val spec = projection.descriptor
        return when (spec.runCapability) {
        RunCapability.PRODUCT_ROOT -> RunInputCutoff(
            backendKey = spec.key,
            pluginObjectRef = projection.objectRef,
            payload = payloadRepository.snapshot(),
            finish = finishRepository.snapshot(),
        )

        RunCapability.DIAGNOSTIC_ONLY -> RunInputCutoff(
            backendKey = spec.key,
            pluginObjectRef = projection.objectRef,
            payload = CompletableDeferred(PayloadDefinition(items = emptyList())),
            finish = CompletableDeferred(FinishDefinition()),
        )

        RunCapability.UNRESOLVED -> error("registry backend capability is unresolved")
        }
    }

    /** Resolves only the exact current selectable projection from one READY generation. */
    private fun currentProjection(): ResolvedBackendProjection? {
        val view = catalogView()
        val key = selectedKey()
        return view?.resolve(key)?.takeIf { projection -> projection.descriptor.selectable }
    }

    /** Computes one fail-closed decision from the same executor snapshot used for dispatch. */
    private fun decision(spec: BackendDescriptor?, executor: AttackExecutorState): RunDecision =
        RunAdmissionPolicy.decide(
            spec = spec,
            availability = spec?.let { availability(it.key) } ?: BackendAvailability.NotReady(
                listOf(BackendAvailabilityReason("backend-missing", "No backend is selected.")),
            ),
            context = RunAdmissionContext(
                backendRunInProgress = logHub.isRunning(),
                executor = executor,
                shizukuTypedHostAvailable = dispatch.shizukuTypedHostAvailable,
            ),
        )

    /** Projects current domain facts into the existing presentation state contract. */
    private fun buildContext(
        spec: BackendDescriptor?,
        admission: RunDecision,
        preferences: OperationalPreferences = operationalPreferences.value,
    ): RunSessionState = RunSessionState(
        backendKey = spec?.key,
        backendLabel = spec?.let { descriptor ->
            backendLabel(descriptor.key) ?: descriptor.key
        } ?: "-",
        startAllowed = admission.allowed,
        blockedReasons = admission.reasons,
        abortable = spec?.abortPolicy == AbortPolicy.SAFE_DURING_RUN,
        warnOnRerun = preferences.warnOnRerun,
        showTimestamp = preferences.showLogTimestamp,
        minimumLogLevel = preferences.logLevel.toLogLevel(),
        terminalFontPath = presentation.resolveTerminalFont(preferences.terminalFontPath),
    )
}

/** Maps the storage-neutral setting to the canonical run-log level. */
private fun TerminalLogLevel.toLogLevel(): LogLevel = when (this) {
    TerminalLogLevel.Verbose -> LogLevel.Verbose
    TerminalLogLevel.Debug -> LogLevel.Debug
    TerminalLogLevel.Info -> LogLevel.Info
    TerminalLogLevel.Warn -> LogLevel.Warn
    TerminalLogLevel.Error -> LogLevel.Error
}
