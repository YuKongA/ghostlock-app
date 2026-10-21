package com.ghostlock.app

import android.util.Log
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.ghostlock.app.engine.ktstep.NativePrepare43499Port
import com.ghostlock.app.engine.ktstep.NativePrepareParams
import com.ghostlock.app.engine.ktstep.NativePrepareResult
import com.ghostlock.app.engine.ktstep.SyscallResult
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/** Prepare 端口 AVD 门禁：只验证 handle/params/result，不声明页落点。 */
@RunWith(AndroidJUnit4::class)
class PrepareProbeInstrumentedTest {
    @Test
    fun aPrepareCallReturnsAReport() {
        val port = NativePrepare43499Port()
        assertTrue(port.open() is SyscallResult.Ok)
        val result = port.prepare(
            NativePrepareParams(
                pageSize = 4096,
                mmStructSize = 1024,
                mmSlabOrder = 2,
                collisions = 4,
                threadCount = 4,
                possibleCpus = 8,
                proofCount = 4,
                proofSeed = 0,
                rangeStart = 0xffffff8000000000uL.toLong(),
                rangeEnd = 0xffffffc000000000uL.toLong(),
                counterOffset = 0x1000,
                wakeRounds = 64,
                roundTimeoutUs = 1000,
                headerBytes = 0x1000,
                totalPokes = 32768,
                wordBytes = 8,
                handleBytes = 8,
                preGroups = 1,
                preShapedGroups = 0,
                sprayGroups = 1,
                postGroups = 1,
                leakGroups = 1,
                snitchCollisions = 4,
                maxAttempts = 4,
                pollRounds = 64,
                socketBuffer = 0x100000,
                mainCpu = 0,
                sweepTags = false,
            ),
        )
        port.close()
        Log.i(TAG, "prepare-result=" + result)
        assertTrue(result is NativePrepareResult.Ok)
    }

    private companion object {
        const val TAG = "PrepareProbe"
    }
}
