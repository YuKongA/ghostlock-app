package com.ghostlock.app.backend.transport

import android.content.pm.PackageManager
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.ghostlock.app.BuildConfig
import com.ghostlock.app.domain.run.DiagnosticOutcome
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
    /** DebugProbe uses the same request and terminal schema in the shell host. */
    @Test
    fun debugProbeCompletesThroughShizukuRunHost() = runBlocking {
        assumeTrue(BuildConfig.DEBUG)
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
            val outcome = ShizukuRunHostClient(context).run(fixture.request)
            assertTrue(outcome is RunHostClientOutcome.Terminal)
            val report = (outcome as RunHostClientOutcome.Terminal).snapshot.toRunReport()
            assertTrue(report.diagnosticOutcome == DiagnosticOutcome.PASS)
            assertFalse(report.isProductSuccess)
        }
    }
}
