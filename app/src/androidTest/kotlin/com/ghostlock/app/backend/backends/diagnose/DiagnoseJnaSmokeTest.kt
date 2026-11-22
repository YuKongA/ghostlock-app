package com.ghostlock.app.backend.backends.diagnose

import android.content.pm.PackageManager
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.ghostlock.app.data.runhost.client.AppRunHostClient
import com.ghostlock.app.core.runhost.RunHostIdentity
import com.ghostlock.app.data.runhost.client.RunHostLogConnector
import com.ghostlock.app.data.runhost.client.RunHostClientOutcome
import com.ghostlock.app.data.runhost.RunHostTestFixture
import com.ghostlock.app.data.runhost.client.ShizukuRunHostClient
import com.ghostlock.app.domain.run.DiagnosticOutcome
import com.ghostlock.app.domain.run.LogConnectorEnd
import com.ghostlock.app.domain.run.LogDraft
import com.ghostlock.app.domain.run.LogOrigin
import com.ghostlock.app.domain.run.RunTerminal
import com.ghostlock.app.domain.run.StageState
import kotlinx.coroutines.async
import kotlinx.coroutines.delay
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import kotlinx.coroutines.withTimeoutOrNull
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith
import rikka.shizuku.Shizuku

/** On-device gates for the builtin package, plugin-owned JNA/DSO, and both RunHost domains. */
@RunWith(AndroidJUnit4::class)
class DiagnoseJnaSmokeTest {
    /** App RunHost loads the catalog-installed builtin archive and executes plugin-owned native code. */
    @Test
    fun appFlowHostRoundTrip() = runBlocking {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        RunHostTestFixture.create(context).use { fixture ->
            val logs = mutableListOf<LogDraft>()
            val connector = RunHostLogConnector(
                RunHostIdentity(fixture.request.runId, fixture.request.generation),
            )
            val collection = async { connector.collect { record -> logs += record } }
            val outcome = AppRunHostClient(context).run(fixture.request, connector)
            assertEquals(LogConnectorEnd.Drained, collection.await())
            assertDiagnoseTerminal(outcome, logs)
        }
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

    private companion object {
        const val SHIZUKU_POLL_MS: Long = 100L
        const val SHIZUKU_WAIT_TIMEOUT_MS: Long = 5_000L
    }
}
