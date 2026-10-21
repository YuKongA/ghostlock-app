package com.ghostlock.app

import android.util.Log
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.ghostlock.app.engine.ktstep.NativeKernelSyscalls
import com.ghostlock.app.engine.ktstep.PiActorsConfig
import com.ghostlock.app.engine.ktstep.PiProbeRunner
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/** K0b 真机探针：只记录读数，不把 requeue 当成 root。 */
@RunWith(AndroidJUnit4::class)
class K0bProbeInstrumentedTest {
    @Test
    fun theProbeProducesReadableLines() {
        val report = PiProbeRunner(
            NativeKernelSyscalls(),
            PiActorsConfig(
                parkTimeoutNs = 2_000_000_000L,
                joinTimeoutMs = 3_000L,
                requeueSettleMs = 500L,
            ),
            phase = { Log.i(TAG, "phase:" + it) },
        ).run()
        val lines = report.logLines()
        lines.forEach { Log.i(TAG, it) }

        assertNull(report.refusal)
        assertNotNull(report.actors)
        assertTrue(lines.any { it.startsWith("k0b-page-write=false") })
        assertTrue(lines.any { it.startsWith("k0b-root=false") })
    }

    private companion object {
        const val TAG = "K0bProbe"
    }
}
