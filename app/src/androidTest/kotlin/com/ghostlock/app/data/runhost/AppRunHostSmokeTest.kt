package com.ghostlock.app.data.runhost

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.ghostlock.app.core.runhost.RunHostIdentity
import com.ghostlock.app.data.runhost.client.AppRunHostClient
import com.ghostlock.app.data.runhost.client.RunHostClientOutcome
import com.ghostlock.app.data.runhost.client.RunHostLogConnector
import com.ghostlock.app.domain.run.DiagnosticOutcome
import com.ghostlock.app.domain.run.LogConnectorEnd
import com.ghostlock.app.domain.run.LogDraft
import com.ghostlock.app.domain.run.LogLevel
import com.ghostlock.app.domain.run.LogOrigin
import com.ghostlock.app.domain.run.RunTerminal
import com.ghostlock.app.domain.run.StageState
import kotlinx.coroutines.async
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/** Typed App-process RunHost smoke using the production client and service. */
@RunWith(AndroidJUnit4::class)
class AppRunHostSmokeTest {
    /** Diagnose crosses Kotlin, Native, and Shell sources and never becomes product success. */
    @Test
    fun diagnoseCompletesThroughAppRunHost() = runBlocking {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        RunHostTestFixture.create(context).use { fixture ->
            val logs = mutableListOf<LogDraft>()
            val connector = RunHostLogConnector(
                RunHostIdentity(fixture.request.runId, fixture.request.generation),
            )
            val collection = async { connector.collect { record -> logs += record } }
            val outcome = AppRunHostClient(context).run(fixture.request, connector)
            assertTrue(collection.await() == LogConnectorEnd.Drained)
            assertTrue(outcome is RunHostClientOutcome.Terminal)
            val report = (outcome as RunHostClientOutcome.Terminal).snapshot.toRunReport()
            assertTrue(report.diagnosticOutcome == DiagnosticOutcome.PASS)
            assertTrue(report.terminal == RunTerminal.DIAGNOSTIC_COMPLETE)
            assertTrue(report.backendPrimitive.state == StageState.SUCCESS)
            assertTrue(report.rootScriptDispatch.state == StageState.SUCCESS)
            assertTrue(report.backendCleanup.state == StageState.SUCCESS)
            assertFalse(report.isProductSuccess)
            assertTrue(logs.any { record ->
                record.message == "diagnose:start" &&
                    record.origin == LogOrigin.Kotlin &&
                    record.level == LogLevel.Info
            })
            assertTrue(logs.any { record ->
                record.message == "diagnose:native:roundtrip-ok" &&
                    record.origin == LogOrigin.Native &&
                    record.level == LogLevel.Info
            })
            assertTrue(logs.any { record ->
                record.message == "diagnose:shell:handoff-ok" &&
                    record.origin == LogOrigin.Shell &&
                    record.level == LogLevel.Info
            })
        }
    }
}
