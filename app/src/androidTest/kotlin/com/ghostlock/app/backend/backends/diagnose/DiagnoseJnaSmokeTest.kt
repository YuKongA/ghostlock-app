package com.ghostlock.app.backend.backends.diagnose

import android.content.pm.PackageManager
import android.os.Process
import android.system.Os
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.ghostlock.app.backend.backends.diagnose.jna.DiagnoseJnaLibrary
import com.ghostlock.app.backend.backends.diagnose.jna.DiagnoseNativeBinding
import com.ghostlock.app.composition.backend.BackendRegistry
import com.ghostlock.app.data.runhost.client.AppRunHostClient
import com.ghostlock.app.core.runhost.RunHostIdentity
import com.ghostlock.app.data.runhost.client.RunHostLogConnector
import com.ghostlock.app.data.runhost.client.RunHostClientOutcome
import com.ghostlock.app.data.runhost.RunHostTestFixture
import com.ghostlock.app.data.runhost.client.ShizukuRunHostClient
import com.ghostlock.app.core.run.LogHub
import com.ghostlock.app.core.run.RunInputFreezer
import com.ghostlock.app.data.run.AndroidRunSessionAdapter
import com.ghostlock.app.data.run.AndroidRunWorkspace
import com.ghostlock.app.data.run.RootManagerArtifactResolver
import com.ghostlock.app.data.run.RootArtifactStagingAdapter
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
import com.ghostlock.app.domain.run.RunCommand
import com.ghostlock.app.domain.run.LogDocumentFormatter
import com.ghostlock.app.domain.run.LogConnectorEnd
import com.ghostlock.app.domain.run.LogDraft
import com.ghostlock.app.domain.run.LogOrigin
import com.ghostlock.app.domain.run.RunSessionPhase
import com.ghostlock.app.domain.run.RunSessionState
import com.ghostlock.app.domain.run.RunTerminal
import com.ghostlock.app.domain.run.StageState
import com.ghostlock.app.domain.settings.ObserveOperationalPreferencesUseCase
import com.ghostlock.app.domain.settings.OperationalPreferences
import com.ghostlock.app.domain.settings.OperationalPreferencesPort
import kotlinx.coroutines.async
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.delay
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import kotlinx.coroutines.withTimeoutOrNull
import kotlinx.coroutines.yield
import java.util.UUID
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith
import rikka.shizuku.Shizuku

/** On-device gates for the real JNA mapping, project DSO, native fd log, and both RunHost domains. */
@RunWith(AndroidJUnit4::class)
class DiagnoseJnaSmokeTest {
    /** Five signed representatives preserve every raw nonce bit through the release-coupled direct mapping. */
    @Test
    fun directJnaNonceAndHandoffRoundTrip() = runBlocking {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val identity = RunHostIdentity(UUID.randomUUID().toString().replace("-", ""), 1L)
        val workspace = AndroidRunWorkspace.createApp(context, identity, BACKEND_KEY)
        RootArtifactStagingAdapter().publishDiagnosticArtifact(workspace, identity)
        val previousHome = System.getenv(GHOSTLOCK_HOME)
        val binding = DiagnoseNativeBinding()
        try {
            Os.setenv(GHOSTLOCK_HOME, workspace.root.absolutePath, true)
            listOf(Long.MIN_VALUE, -1L, 0L, 1L, Long.MAX_VALUE).forEach { nonce ->
                val logs = mutableListOf<LogDraft>()
                val chain = binding.runChain(logs = { record -> logs += record }, nonce = nonce)
                assertEquals(0, chain.status)
                assertEquals(Process.myUid(), chain.uid)
                assertEquals(Process.myPid(), chain.pid)
                assertEquals(nonce, chain.echoedNonce)
                assertEquals(0, chain.handoffStatus)
                assertTrue(chain.nativeLogValid)
                assertTrue(chain.shellLogValid)
                assertEquals(1, logs.count { record ->
                    record.origin == LogOrigin.Native &&
                        record.message == "diagnose:native:roundtrip-ok"
                })
                assertEquals(1, logs.count { record ->
                    record.origin == LogOrigin.Shell &&
                        record.message == "diagnose:shell:handoff-ok"
                })
            }
        } finally {
            binding.close()
            if (previousHome == null) {
                Os.unsetenv(GHOSTLOCK_HOME)
            } else {
                Os.setenv(GHOSTLOCK_HOME, previousHome, true)
            }
            assertTrue(workspace.deleteAfterTerminal())
        }
    }

    /** App flowhost routes the real Diagnose run through AndroidRunSessionAdapter and the sole LogHub. */
    @Test
    fun appFlowHostRoundTrip() = runBlocking {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val logHub = LogHub(LogDocumentFormatter())
        val adapter = AndroidRunSessionAdapter(
            context = context,
            registryEntries = BackendRegistry.descriptors,
            availability = BackendRegistry::availability,
            backendLabel = { key -> if (key == BACKEND_KEY) "Diagnose" else null },
            selectedKey = { BACKEND_KEY },
            logHub = logHub,
            formatter = LogDocumentFormatter(),
            freezer = diagnosticFreezer(context),
            appClient = AppRunHostClient(context),
            shizukuClient = null,
            operationalPreferencesPort = diagnosticPreferences(),
            applicationScope = this,
            observePrivilege = appPrivilege(Process.myUid()),
        )

        adapter.refresh()
        assertTrue(adapter.start(RunCommand(BACKEND_KEY)).await())
        val terminal = withTimeout(WAIT_TIMEOUT_MS) {
            logHub.state.first { state -> state.phase == RunSessionPhase.Done }
        }
        withTimeout(WAIT_TIMEOUT_MS) {
            while (logHub.isRunning()) yield()
        }

        assertLogHubDiagnoseTerminal(terminal)
    }

    /** Shizuku UserService loads both DSOs and executes the same chain in uid 2000 shell domain. */
    @Test
    fun shizukuRoundTrip() = runBlocking {
        assumeTrue(shizukuReadinessDescription(), awaitShizukuReady())
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        RunHostTestFixture.create(context).use { fixture ->
            val logs = mutableListOf<LogDraft>()
            val connector = RunHostLogConnector(
                RunHostIdentity(fixture.request.runId, fixture.request.generation),
            )
            val collection = async { connector.collect { record -> logs += record } }
            val outcome = ShizukuRunHostClient(context).run(fixture.request, connector)
            assertEquals(LogConnectorEnd.Drained, collection.await())
            assertDiagnoseTerminal(outcome, logs)
        }
    }

    /** Waits only for the already-running Shizuku binder to reach this fresh instrumentation process. */
    private suspend fun awaitShizukuReady(): Boolean = withTimeoutOrNull(SHIZUKU_WAIT_TIMEOUT_MS) {
        while (
            !runCatching {
                Shizuku.pingBinder() &&
                    Shizuku.checkSelfPermission() == PackageManager.PERMISSION_GRANTED &&
                    Shizuku.getUid() == 2000 &&
                    Shizuku.getSELinuxContext()?.contains("u:r:shell:s0") == true
            }.getOrDefault(false)
        ) {
            delay(SHIZUKU_POLL_MS)
        }
        true
    } ?: false

    /** Returns exact post-wait Shizuku facts when this environment skips the shell-domain gate. */
    private fun shizukuReadinessDescription(): String = runCatching {
        "ping=${Shizuku.pingBinder()};permission=${Shizuku.checkSelfPermission()};" +
            "uid=${Shizuku.getUid()};domain=${Shizuku.getSELinuxContext()}"
    }.getOrElse { problem -> "query=${problem::class.java.simpleName}" }

    /** Checks the exact typed report and one record from each actual source adapter. */
    private fun assertDiagnoseTerminal(
        outcome: RunHostClientOutcome,
        logs: List<LogDraft>,
    ) {
        assertTrue(outcome is RunHostClientOutcome.Terminal)
        val report = (outcome as RunHostClientOutcome.Terminal).snapshot.toRunReport()
        assertEquals(DiagnosticOutcome.PASS, report.diagnosticOutcome)
        assertEquals(StageState.SUCCESS, report.backendPrimitive.state)
        assertEquals(StageState.SUCCESS, report.rootScriptDispatch.state)
        assertEquals(StageState.SUCCESS, report.backendCleanup.state)
        assertEquals(RunTerminal.DIAGNOSTIC_COMPLETE, report.terminal)
        assertFalse(report.isProductSuccess)
        assertEquals(1, logs.count { record ->
            record.origin == LogOrigin.Native &&
                record.message == "diagnose:native:roundtrip-ok"
        })
        assertEquals(1, logs.count { record ->
            record.origin == LogOrigin.Shell &&
                record.message == "diagnose:shell:handoff-ok"
        })
        assertTrue(logs.any { record ->
            record.origin == LogOrigin.Kotlin && record.message == "diagnose:start"
        })
    }

    /** Checks that the application hub retained one record from every source and the typed diagnostic report. */
    private fun assertLogHubDiagnoseTerminal(state: RunSessionState) {
        val report = requireNotNull(state.report)
        assertEquals(DiagnosticOutcome.PASS, report.diagnosticOutcome)
        assertEquals(StageState.SUCCESS, report.backendPrimitive.state)
        assertEquals(StageState.SUCCESS, report.rootScriptDispatch.state)
        assertEquals(StageState.SUCCESS, report.backendCleanup.state)
        assertEquals(RunTerminal.DIAGNOSTIC_COMPLETE, report.terminal)
        assertFalse(report.isProductSuccess)
        assertEquals(1, state.entries.count { record ->
            record.origin == LogOrigin.Native && record.message == "diagnose:native:roundtrip-ok"
        })
        assertEquals(1, state.entries.count { record ->
            record.origin == LogOrigin.Shell && record.message == "diagnose:shell:handoff-ok"
        })
        assertTrue(state.entries.any { record ->
            record.origin == LogOrigin.Kotlin && record.message == "diagnose:start"
        })
    }

    /** Builds the production freezer with fail-fast repositories that Diagnose must never read. */
    private fun diagnosticFreezer(context: android.content.Context): RunInputFreezer = RunInputFreezer(
        context = context,
        payloadRepository = object : PayloadRepository {
            override fun submit(
                transform: (PayloadDefinition) -> PayloadDefinition,
            ): kotlinx.coroutines.Deferred<PayloadDefinition> = error("Diagnose submit Payload")
            override fun snapshot(): kotlinx.coroutines.Deferred<PayloadDefinition> =
                error("Diagnose snapshot Payload")
        },
        finishRepository = object : FinishRepository {
            override fun submit(
                transform: (FinishDefinition) -> FinishDefinition,
            ): kotlinx.coroutines.Deferred<FinishDefinition> = error("Diagnose submit Finish")
            override fun snapshot(): kotlinx.coroutines.Deferred<FinishDefinition> =
                error("Diagnose snapshot Finish")
        },
        rootManagerResolver = RootManagerArtifactResolver(context),
    )

    /** Supplies one frozen App-executor fact without touching device preferences. */
    private fun appPrivilege(uid: Int): ObservePrivilegeUseCase = ObservePrivilegeUseCase(
        object : PrivilegePort {
            override val state = MutableStateFlow(
                PrivilegeState(
                    selectedExecutor = AttackExecutorKind.APP,
                    appUid = uid,
                    shizukuAvailable = false,
                    shizukuGranted = false,
                ),
            )

            override fun refresh() = Unit

            override suspend fun select(kind: AttackExecutorKind): ExecutorSelectionResult =
                if (kind == AttackExecutorKind.APP) {
                    ExecutorSelectionResult.SELECTED
                } else {
                    ExecutorSelectionResult.UNAVAILABLE
                }
        },
    )

    /** Disables export so the gate observes LogHub ordering without creating a MediaStore row. */
    private fun diagnosticPreferences(): OperationalPreferencesPort =
        object : OperationalPreferencesPort {
            override val preferences = MutableStateFlow(
                OperationalPreferences(autoExportLogs = false, warnOnRerun = false),
            )

            override fun submit(
                transform: (OperationalPreferences) -> OperationalPreferences,
            ): kotlinx.coroutines.Deferred<OperationalPreferences> {
                val value = transform(preferences.value)
                preferences.value = value
                return kotlinx.coroutines.CompletableDeferred(value)
            }

            override suspend fun awaitInitial(): OperationalPreferences = preferences.value

            override fun snapshot(): kotlinx.coroutines.Deferred<OperationalPreferences> =
                kotlinx.coroutines.CompletableDeferred(preferences.value)

            override fun importTerminalFont(uri: String): kotlinx.coroutines.Deferred<Boolean> =
                kotlinx.coroutines.CompletableDeferred(false)

            override fun selectLogExportDirectory(uri: String): kotlinx.coroutines.Deferred<Boolean> =
                kotlinx.coroutines.CompletableDeferred(false)
        }

    private companion object {
        const val BACKEND_KEY: String = "DIAGNOSE"
        const val GHOSTLOCK_HOME: String = "GHOSTLOCK_HOME"
        const val SHIZUKU_POLL_MS: Long = 100L
        const val SHIZUKU_WAIT_TIMEOUT_MS: Long = 5_000L
        const val WAIT_TIMEOUT_MS: Long = 20_000L
    }
}
