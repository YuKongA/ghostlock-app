package com.ghostlock.app.backend.transport

import android.content.pm.PackageManager
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.ghostlock.app.domain.run.DiagnosticOutcome
import com.ghostlock.app.domain.run.LogOrigin
import com.ghostlock.app.domain.run.StageState
import com.ghostlock.app.run.RunHostLogRecord
import kotlinx.coroutines.runBlocking
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
        assumeTrue(
            runCatching {
                Shizuku.pingBinder() &&
                    Shizuku.checkSelfPermission() == PackageManager.PERMISSION_GRANTED &&
                    Shizuku.getUid() == 2000 &&
                    Shizuku.getSELinuxContext()?.contains("u:r:shell:s0") == true
            }.getOrDefault(false),
        )
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        RunHostTestFixture.create(context).use { fixture ->
            val logs = mutableListOf<RunHostLogRecord>()
            val outcome = ShizukuRunHostClient(context).run(fixture.request) { record -> logs += record }
            assertTrue(outcome is RunHostClientOutcome.Terminal)
            val report = (outcome as RunHostClientOutcome.Terminal).snapshot.toRunReport()
            assertTrue(report.diagnosticOutcome == DiagnosticOutcome.PASS)
            assertTrue(report.backendPrimitive.state == StageState.SUCCESS)
            assertTrue(report.rootScriptDispatch.state == StageState.SUCCESS)
            assertTrue(report.backendCleanup.state == StageState.SUCCESS)
            assertFalse(report.isProductSuccess)
            assertTrue(logs.count { record ->
                record.origin == LogOrigin.Native.wireValue &&
                    record.message == "diagnose:native:roundtrip-ok"
            } == 1)
            assertTrue(logs.count { record ->
                record.origin == LogOrigin.Shell.wireValue &&
                    record.message == "diagnose:shell:handoff-ok"
            } == 1)
        }
    }
}
