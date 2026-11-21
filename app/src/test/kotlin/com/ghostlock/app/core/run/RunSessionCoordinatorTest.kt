package com.ghostlock.app.core.run

import com.ghostlock.app.core.log.LogHub
import com.ghostlock.app.core.runhost.RunHostIdentity
import com.ghostlock.app.domain.backend.AbortPolicy
import com.ghostlock.app.domain.backend.BackendAvailability
import com.ghostlock.app.domain.backend.BackendDescriptor
import com.ghostlock.app.domain.backend.BackendSelectionPolicy
import com.ghostlock.app.domain.backend.ExecutionDomain
import com.ghostlock.app.domain.finish.FinishDefinition
import com.ghostlock.app.domain.finish.FinishRepository
import com.ghostlock.app.domain.payload.PayloadDefinition
import com.ghostlock.app.domain.payload.PayloadRepository
import com.ghostlock.app.domain.privilege.AttackExecutorKind
import com.ghostlock.app.domain.privilege.ExecutorSelectionResult
import com.ghostlock.app.domain.privilege.ObservePrivilegeUseCase
import com.ghostlock.app.domain.privilege.PrivilegePort
import com.ghostlock.app.domain.privilege.PrivilegeState
import com.ghostlock.app.domain.run.DiagnosticOutcome
import com.ghostlock.app.domain.run.LogConnector
import com.ghostlock.app.domain.run.LogConnectorEnd
import com.ghostlock.app.domain.run.LogDocumentFormatter
import com.ghostlock.app.domain.run.LogFilePrefix
import com.ghostlock.app.domain.run.LogFileSink
import com.ghostlock.app.domain.run.LogFormatConfig
import com.ghostlock.app.domain.run.LogSinkBinding
import com.ghostlock.app.domain.run.LogSinkEvent
import com.ghostlock.app.domain.run.LogSinkRequest
import com.ghostlock.app.domain.run.LogSinkRole
import com.ghostlock.app.domain.run.RunCapability
import com.ghostlock.app.domain.run.RunCommand
import com.ghostlock.app.domain.run.RunExecutionResult
import com.ghostlock.app.domain.run.RunPresentationPort
import com.ghostlock.app.domain.run.RunReport
import com.ghostlock.app.domain.run.RunSessionState
import com.ghostlock.app.domain.run.RunTerminal
import com.ghostlock.app.domain.run.StageResult
import com.ghostlock.app.domain.settings.OperationalPreferences
import com.ghostlock.app.domain.settings.OperationalPreferencesPort
import java.nio.file.Files
import java.util.concurrent.atomic.AtomicInteger
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Deferred
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.runTest
import kotlinx.coroutines.withTimeout
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test

/** Application coordinator ownership, cutoff, fixed-target, and pre-commit failure tests. */
class RunSessionCoordinatorTest {
    /** Payload, Finish, settings, and executor are captured before asynchronous preparation resumes. */
    @Test
    fun actorCutoffsAndExecutorTargetDoNotDrift() = runBlocking {
        val payload = FakePayloadRepository(PayloadDefinition(safeMode = true))
        val finish = FakeFinishRepository(FinishDefinition())
        val preferences = FakeOperationalPreferencesPort()
        val preferenceGate = CompletableDeferred<OperationalPreferences>()
        preferences.nextSnapshot = preferenceGate
        val privilege = FakePrivilegePort(AttackExecutorKind.SHIZUKU)
        var capturedTarget: AttackExecutorKind? = null
        var capturedPayload: PayloadDefinition? = null
        var capturedFinish: FinishDefinition? = null
        val dispatch = FakeDispatch { _, target, cutoff ->
            capturedTarget = target
            capturedPayload = cutoff.payload.await()
            capturedFinish = cutoff.finish.await()
            RunDispatchPreparation.Failed(RunDispatchFailureStage.FREEZE, "Synthetic")
        }
        val fixture = fixture(payload, finish, preferences, privilege, dispatch)

        val accepted = fixture.coordinator.start(RunCommand(SPEC.key))
        payload.current = PayloadDefinition(safeMode = false)
        finish.current = FinishDefinition(items = emptyList())
        privilege.setExecutor(AttackExecutorKind.APP)
        preferenceGate.complete(preferences.current)

        assertFalse(accepted.await())
        assertEquals(AttackExecutorKind.SHIZUKU, capturedTarget)
        assertEquals(true, capturedPayload?.safeMode)
        assertEquals(FinishDefinition(), capturedFinish)
    }

    /** Both explicit Android preparation stages preserve their stable blocked-reason prefixes. */
    @Test
    fun preparationFailuresRemainDistinctAndNeverCreateSinks() = runBlocking {
        RunDispatchFailureStage.entries.forEach { stage ->
            val providerCalls = AtomicInteger(0)
            val dispatch = FakeDispatch { _, _, _ -> RunDispatchPreparation.Failed(stage, "Synthetic") }
            val fixture = fixture(
                dispatch = dispatch,
                sinkProvider = LogSinkProvider { _, _ ->
                    providerCalls.incrementAndGet()
                    emptyList()
                },
            )
            assertFalse(fixture.coordinator.start(RunCommand(SPEC.key)).await())
            val expected = when (stage) {
                RunDispatchFailureStage.FREEZE -> "Run input freeze failed: Synthetic"
                RunDispatchFailureStage.REQUEST_ASSEMBLY ->
                    "Typed RunHost request assembly failed: Synthetic"
            }
            assertEquals(listOf(expected), fixture.hub.state.value.blockedReasons)
            assertEquals(0, providerCalls.get())
        }
    }

    /** An unmodeled provider failure propagates and closes the untransferred dispatch lease once. */
    @Test
    fun providerThrowClosesPreparedLeaseOnce() {
        val lease = FakeLease()
        val fixture = fixture(
            dispatch = preparedDispatch(lease),
            sinkProvider = LogSinkProvider { _, _ -> error("provider") },
        )
        val failure = assertThrows(IllegalStateException::class.java) {
            runBlocking { fixture.coordinator.start(RunCommand(SPEC.key)).await() }
        }
        assertEquals("provider", failure.message)
        assertEquals(1, lease.closeCount.get())
        assertEquals(0, lease.dispatchCount.get())
    }

    /** LogHub's invalid pre-commit sink throw closes the lease without starting dispatch. */
    @Test
    fun logHubPreCommitThrowClosesPreparedLeaseOnce() {
        val lease = FakeLease()
        val fixture = fixture(
            dispatch = preparedDispatch(lease),
            sinkProvider = LogSinkProvider { _, _ -> emptyList() },
        )
        assertThrows(IllegalArgumentException::class.java) {
            runBlocking { fixture.coordinator.start(RunCommand(SPEC.key)).await() }
        }
        assertEquals(1, lease.closeCount.get())
        assertEquals(0, lease.dispatchCount.get())
    }

    /** A run accepted during Android preparation makes the later LogHub rejection close both owners. */
    @Test
    fun logHubFalseAbortsSuppliedSinkAndClosesLease() = runBlocking {
        val prepareGate = CompletableDeferred<Unit>()
        val lease = FakeLease()
        val dispatch = FakeDispatch { _, _, _ ->
            prepareGate.await()
            RunDispatchPreparation.Prepared(lease)
        }
        val providedSink = RecordingFileSink()
        val fixture = fixture(
            dispatch = dispatch,
            sinkProvider = LogSinkProvider { _, _ ->
                listOf(LogSinkBinding(LogSinkRole.PRIVATE_FILE, providedSink))
            },
        )
        val pending = fixture.coordinator.start(RunCommand(SPEC.key))
        val directRelease = CompletableDeferred<Unit>()
        assertTrue(
            fixture.hub.start(
                context = runningContext(),
                sinkRequest = sinkRequest(),
                storageSinks = listOf(
                    LogSinkBinding(LogSinkRole.PRIVATE_FILE, RecordingFileSink()),
                ),
            ) {
                directRelease.await()
                RunExecutionResult.Completed(diagnosticReport())
            },
        )
        prepareGate.complete(Unit)

        assertFalse(pending.await())
        assertEquals(1, lease.closeCount.get())
        assertEquals(0, lease.dispatchCount.get())
        assertEquals(1, providedSink.abortCount.get())
        directRelease.complete(Unit)
        Unit
    }

    /** Accepted commit transfers one lease, starts dispatch once, and terminates it once. */
    @Test
    fun acceptedCommitDispatchesAndClosesLeaseOnce() = runBlocking {
        val lease = FakeLease()
        val fixture = fixture(dispatch = preparedDispatch(lease))

        assertTrue(fixture.coordinator.start(RunCommand(SPEC.key)).await())
        waitFor { lease.closeCount.get() == 1 }

        assertEquals(1, lease.dispatchCount.get())
        assertEquals(1, lease.closeCount.get())
        assertEquals(RunTerminal.DIAGNOSTIC_COMPLETE, fixture.hub.state.value.report?.terminal)
    }

    /** Cancellation before the accepted execute body enters still closes its lease and releases the slot. */
    @Test
    fun acceptedAbortBeforeExecuteEntryClosesLeaseAndReleasesSlot() = runTest {
        val dispatcher = StandardTestDispatcher(testScheduler)
        val hub = LogHub(LogDocumentFormatter(), dispatcher)
        val lease = FakeLease()
        val fixture = fixture(
            dispatch = preparedDispatch(lease),
            hub = hub,
            applicationScope = this,
        )

        assertTrue(fixture.coordinator.start(RunCommand(SPEC.key)).await())
        assertEquals(0, lease.dispatchCount.get())
        assertTrue(hub.requestAbort("abort-now"))
        testScheduler.runCurrent()

        assertEquals(1, lease.closeCount.get())
        assertEquals(0, lease.dispatchCount.get())
        assertFalse(hub.isRunning())
        assertEquals(com.ghostlock.app.domain.run.RunSessionPhase.Aborted, hub.state.value.phase)

        val replacement = RecordingFileSink()
        assertTrue(
            hub.start(
                context = runningContext().copy(abortable = true),
                sinkRequest = sinkRequest(),
                storageSinks = listOf(LogSinkBinding(LogSinkRole.PRIVATE_FILE, replacement)),
            ) { RunExecutionResult.Completed(diagnosticReport()) },
        )
        assertTrue(hub.requestAbort("replacement-abort"))
        testScheduler.runCurrent()
        assertFalse(hub.isRunning())
    }

    /** Creates one coordinator fixture with only the requested failure seam replaced. */
    private fun fixture(
        payload: FakePayloadRepository = FakePayloadRepository(PayloadDefinition()),
        finish: FakeFinishRepository = FakeFinishRepository(FinishDefinition()),
        preferences: FakeOperationalPreferencesPort = FakeOperationalPreferencesPort(),
        privilege: FakePrivilegePort = FakePrivilegePort(AttackExecutorKind.APP),
        dispatch: RunDispatchPort,
        sinkProvider: LogSinkProvider = LogSinkProvider { _, _ ->
            listOf(LogSinkBinding(LogSinkRole.PRIVATE_FILE, RecordingFileSink()))
        },
        hub: LogHub = LogHub(LogDocumentFormatter()),
        applicationScope: CoroutineScope = CoroutineScope(SupervisorJob() + Dispatchers.Default),
    ): Fixture {
        val coordinator = RunSessionCoordinator(
            registryEntries = listOf(SPEC),
            availability = { BackendAvailability.Ready() },
            backendLabel = { "Fixture" },
            selectedKey = { SPEC.key },
            logHub = hub,
            dispatch = dispatch,
            sinkProvider = sinkProvider,
            presentation = FakePresentation,
            operationalPreferencesPort = preferences,
            payloadRepository = payload,
            finishRepository = finish,
            applicationScope = applicationScope,
            observePrivilege = ObservePrivilegeUseCase(privilege),
        )
        return Fixture(coordinator, hub)
    }

    /** Returns a dispatch port that always yields [lease]. */
    private fun preparedDispatch(lease: RunDispatchLease): RunDispatchPort =
        FakeDispatch { _, _, _ -> RunDispatchPreparation.Prepared(lease) }

    /** Waits for one asynchronous LogHub state transition. */
    private suspend fun waitFor(condition: () -> Boolean) = withTimeout(5_000L) {
        while (!condition()) delay(5L)
    }

    /** Existing startable context used to create a deliberate LogHub acceptance race. */
    private fun runningContext(): RunSessionState = RunSessionState(
        backendKey = SPEC.key,
        backendLabel = "Fixture",
        startAllowed = true,
    )

    /** Existing sink identity used by the deliberate direct LogHub run. */
    private fun sinkRequest(): LogSinkRequest = LogSinkRequest(
        enabled = false,
        runId = IDENTITY.runId,
        generation = IDENTITY.generation,
        backendKey = SPEC.key,
        relativePath = "Download/ghostlock-log",
        formatConfig = LogFormatConfig(false),
    )

    /** Legal diagnostic completion returned by fake accepted dispatches. */
    private fun diagnosticReport(): RunReport = diagnosticReportStatic()

    private data class Fixture(
        val coordinator: RunSessionCoordinator,
        val hub: LogHub,
    )

    /** Mutable privilege authority used to prove target capture. */
    private class FakePrivilegePort(initial: AttackExecutorKind) : PrivilegePort {
        private val mutable = MutableStateFlow(state(initial))
        override val state: StateFlow<PrivilegeState> = mutable

        fun setExecutor(kind: AttackExecutorKind) {
            mutable.value = state(kind)
        }

        override fun refresh() = Unit

        override suspend fun select(kind: AttackExecutorKind): ExecutorSelectionResult {
            setExecutor(kind)
            return ExecutorSelectionResult.SELECTED
        }

        private fun state(kind: AttackExecutorKind): PrivilegeState = PrivilegeState(
            selectedExecutor = kind,
            appUid = 10_000,
            shizukuAvailable = true,
            shizukuGranted = true,
        )
    }

    /** Actor-like Payload fake whose snapshot returns the value captured at invocation time. */
    private class FakePayloadRepository(initial: PayloadDefinition) : PayloadRepository {
        var current: PayloadDefinition = initial

        override fun submit(
            transform: (PayloadDefinition) -> PayloadDefinition,
        ): Deferred<PayloadDefinition> {
            current = transform(current)
            return CompletableDeferred(current)
        }

        override fun snapshot(): Deferred<PayloadDefinition> = CompletableDeferred(current)
    }

    /** Actor-like Finish fake whose snapshot returns the value captured at invocation time. */
    private class FakeFinishRepository(initial: FinishDefinition) : FinishRepository {
        var current: FinishDefinition = initial

        override fun submit(
            transform: (FinishDefinition) -> FinishDefinition,
        ): Deferred<FinishDefinition> {
            current = transform(current)
            return CompletableDeferred(current)
        }

        override fun snapshot(): Deferred<FinishDefinition> = CompletableDeferred(current)
    }

    /** Operational settings fake with an optional externally controlled snapshot completion. */
    private class FakeOperationalPreferencesPort : OperationalPreferencesPort {
        var current = OperationalPreferences(autoExportLogs = false)
        var nextSnapshot: Deferred<OperationalPreferences>? = null
        private val mutable = MutableStateFlow(current)
        override val preferences: StateFlow<OperationalPreferences> = mutable

        override fun submit(
            transform: (OperationalPreferences) -> OperationalPreferences,
        ): Deferred<OperationalPreferences> {
            current = transform(current)
            mutable.value = current
            return CompletableDeferred(current)
        }

        override suspend fun awaitInitial(): OperationalPreferences = current

        override fun snapshot(): Deferred<OperationalPreferences> =
            nextSnapshot.also { nextSnapshot = null } ?: CompletableDeferred(current)

        override fun importTerminalFont(uri: String): Deferred<Boolean> = CompletableDeferred(false)

        override fun selectLogExportDirectory(uri: String): Deferred<Boolean> = CompletableDeferred(false)
    }

    /** Lambda-backed dispatch seam with the real fixed-target port surface. */
    private class FakeDispatch(
        private val block: suspend (
            BackendDescriptor,
            AttackExecutorKind,
            RunInputCutoff,
        ) -> RunDispatchPreparation,
    ) : RunDispatchPort {
        override val shizukuTypedHostAvailable: Boolean = true

        override suspend fun prepare(
            spec: BackendDescriptor,
            target: AttackExecutorKind,
            cutoff: RunInputCutoff,
        ): RunDispatchPreparation = block(spec, target, cutoff)
    }

    /** Request lease fake that records every close invocation to detect duplicate ownership. */
    private class FakeLease : RunDispatchLease {
        override val identity: RunHostIdentity = IDENTITY
        override val backendKey: String = SPEC.key
        val dispatchCount = AtomicInteger(0)
        val closeCount = AtomicInteger(0)

        override suspend fun dispatch(
            attach: suspend (LogConnector) -> LogConnectorEnd,
        ): RunExecutionResult {
            dispatchCount.incrementAndGet()
            return RunExecutionResult.Completed(diagnosticReportStatic())
        }

        override fun close() {
            closeCount.incrementAndGet()
        }
    }

    /** In-memory private-file role with observable rejection cleanup. */
    private class RecordingFileSink : LogFileSink {
        val abortCount = AtomicInteger(0)
        private val completion = CompletableDeferred<LogFilePrefix>()
        override val finished: Deferred<LogFilePrefix> = completion

        override fun tryEmit(event: LogSinkEvent): Boolean {
            if (event is LogSinkEvent.End && !completion.isCompleted) {
                val file = Files.createTempFile("run-session-log", ".txt").toFile()
                completion.complete(LogFilePrefix(file.absolutePath, 0L))
            }
            return true
        }

        override fun abort(reason: String) {
            abortCount.incrementAndGet()
            if (!completion.isCompleted) completion.completeExceptionally(IllegalStateException(reason))
        }
    }

    private object FakePresentation : RunPresentationPort {
        override fun abortDisabledMessage(): String = "abort-disabled"
        override fun resolveTerminalFont(reference: String?): String? = reference
        override suspend fun shareTerminal(chooserTitle: String): Boolean = true
    }

    private companion object {
        val IDENTITY = RunHostIdentity("0123456789abcdef0123456789abcdef", 1L)
        val SPEC = BackendDescriptor(
            key = "product-test",
            selectionPolicy = BackendSelectionPolicy.SELECTABLE,
            abortPolicy = AbortPolicy.SAFE_DURING_RUN,
            executionDomain = ExecutionDomain.APP_OR_SHIZUKU,
            runCapability = RunCapability.PRODUCT_ROOT,
        )

        fun diagnosticReportStatic(): RunReport = RunReport(
            runCapability = RunCapability.DIAGNOSTIC_ONLY,
            diagnosticOutcome = DiagnosticOutcome.PASS,
            backendPrimitive = StageResult.SUCCESS,
            rootScriptDispatch = StageResult.SKIPPED,
            payloadItems = emptyList(),
            payloadOverall = StageResult.SKIPPED,
            finishItems = emptyList(),
            finishOverall = StageResult.SKIPPED,
            backendCleanup = StageResult.SUCCESS,
            terminal = RunTerminal.DIAGNOSTIC_COMPLETE,
        )
    }
}
