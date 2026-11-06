package com.ghostlock.app.backend.transport

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.ghostlock.app.BuildConfig
import com.ghostlock.app.domain.run.DiagnosticOutcome
import com.ghostlock.app.domain.run.RunLogLevel
import com.ghostlock.app.domain.run.RunLogOrigin
import com.ghostlock.app.domain.run.RunTerminal
import com.ghostlock.app.run.RunHostLogRecord
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith

/** Typed App-process RunHost smoke using the production client and service. */
@RunWith(AndroidJUnit4::class)
class AppRunHostSmokeTest {
    /** DebugProbe reaches a diagnostic terminal and never product success. */
    @Test
    fun debugProbeCompletesThroughAppRunHost() = runBlocking {
        assumeTrue(BuildConfig.DEBUG)
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        RunHostTestFixture.create(context).use { fixture ->
            val logs = mutableListOf<RunHostLogRecord>()
            val outcome = AppRunHostClient(context).run(fixture.request) { record -> logs += record }
            assertTrue(outcome is RunHostClientOutcome.Terminal)
            val report = (outcome as RunHostClientOutcome.Terminal).snapshot.toRunReport()
            assertTrue(report.diagnosticOutcome == DiagnosticOutcome.PASS)
            assertTrue(report.terminal == RunTerminal.DIAGNOSTIC_COMPLETE)
            assertFalse(report.isProductSuccess)
            assertTrue(logs.any { record ->
                record.message == "debug-probe:done" &&
                    record.origin == RunLogOrigin.Kotlin.wireValue &&
                    record.level == RunLogLevel.Debug.wireValue
            })
        }
    }
}
