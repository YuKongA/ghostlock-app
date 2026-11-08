package com.ghostlock.app.data.runhost

import android.content.pm.PackageManager
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.ghostlock.app.core.runhost.RunHostIdentity
import com.ghostlock.app.data.runhost.client.RunHostClientOutcome
import com.ghostlock.app.data.runhost.client.RunHostLogConnector
import com.ghostlock.app.data.runhost.client.ShizukuRunHostClient
import com.ghostlock.app.domain.run.DiagnosticOutcome
import com.ghostlock.app.domain.run.LogConnectorEnd
import com.ghostlock.app.domain.run.LogDraft
import com.ghostlock.app.domain.run.LogOrigin
import com.ghostlock.app.domain.run.StageState
import kotlinx.coroutines.delay
import kotlinx.coroutines.async
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeoutOrNull
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith
import rikka.shizuku.Shizuku

/** Typed Shizuku UserService smoke when a shell-domain service is available. */
@RunWith(AndroidJUnit4::class)
class ShizukuRunHostSmokeTest {
    /** Diagnose uses the same native and shell chain inside the shell-domain UserService. */
    @Test
    fun diagnoseCompletesThroughShizukuRunHost() = runBlocking {
        val ready = withTimeoutOrNull(SHIZUKU_WAIT_TIMEOUT_MS) {
            while (!runCatching {
                Shizuku.pingBinder() &&
                    Shizuku.checkSelfPermission() == PackageManager.PERMISSION_GRANTED &&
                    Shizuku.getUid() == 2000 &&
                    Shizuku.getSELinuxContext()?.contains("u:r:shell:s0") == true
            }.getOrDefault(false)) {
                delay(SHIZUKU_POLL_MS)
            }
            true
        } ?: false
        assumeTrue(shizukuReadinessDescription(), ready)
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        RunHostTestFixture.create(context).use { fixture ->
            val logs = mutableListOf<LogDraft>()
            val connector = RunHostLogConnector(
                RunHostIdentity(fixture.request.runId, fixture.request.generation),
            )
            val collection = async { connector.collect { record -> logs += record } }
            val outcome = ShizukuRunHostClient(context).run(fixture.request, connector)
            assertTrue(collection.await() == LogConnectorEnd.Drained)
            assertTrue(outcome is RunHostClientOutcome.Terminal)
            val report = (outcome as RunHostClientOutcome.Terminal).snapshot.toRunReport()
            assertTrue(report.diagnosticOutcome == DiagnosticOutcome.PASS)
            assertTrue(report.backendPrimitive.state == StageState.SUCCESS)
            assertTrue(report.rootScriptDispatch.state == StageState.SUCCESS)
            assertTrue(report.backendCleanup.state == StageState.SUCCESS)
            assertFalse(report.isProductSuccess)
            assertTrue(logs.count { record ->
                record.origin == LogOrigin.Native &&
                    record.message == "diagnose:native:roundtrip-ok"
            } == 1)
            assertTrue(logs.count { record ->
                record.origin == LogOrigin.Shell &&
                    record.message == "diagnose:shell:handoff-ok"
            } == 1)
        }
    }

    /** Returns exact post-wait Shizuku facts when this environment skips the shell-domain gate. */
    private fun shizukuReadinessDescription(): String = runCatching {
        "ping=${Shizuku.pingBinder()};permission=${Shizuku.checkSelfPermission()};" +
            "uid=${Shizuku.getUid()};domain=${Shizuku.getSELinuxContext()}"
    }.getOrElse { problem -> "query=${problem::class.java.simpleName}" }

    private companion object {
        const val SHIZUKU_POLL_MS: Long = 100L
        const val SHIZUKU_WAIT_TIMEOUT_MS: Long = 5_000L
    }
}
