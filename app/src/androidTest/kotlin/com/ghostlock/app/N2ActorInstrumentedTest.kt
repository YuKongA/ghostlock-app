package com.ghostlock.app

import android.util.Log
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.ghostlock.app.engine.ktstep.KernelFutexWords
import com.ghostlock.app.engine.ktstep.FutexWordsResult
import com.ghostlock.app.engine.ktstep.NativeAttemptParams
import com.ghostlock.app.engine.ktstep.NativeAttemptResult
import com.ghostlock.app.engine.ktstep.NativeJniAttemptPort
import com.ghostlock.app.engine.ktstep.NativeKernelSyscalls
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/** N2 真机门禁：C++ actor batch 必须复现 K0b 的 requeue=1/wait=0。 */
@RunWith(AndroidJUnit4::class)
class N2ActorInstrumentedTest {
    @Test
    fun theNativeBatchMovesOneWaiter() {
        val syscalls = NativeKernelSyscalls()
        val words = when (val made = KernelFutexWords.create(syscalls, waitTimeoutNs = -1L)) {
            is FutexWordsResult.Ok -> made.words
            is FutexWordsResult.Errno -> error("words:" + made.code)
        }
        val port = NativeJniAttemptPort()
        assertEquals(com.ghostlock.app.engine.ktstep.SyscallResult.Ok(1L), port.open())
        val params = NativeAttemptParams(
            chainAddress = words.chain.address,
            waitAddress = words.wait.address,
            targetAddress = words.target.address,
            pageSinkAddress = words.mapping,
            chainValue = 0,
            waitValue = 0,
            targetValue = 0,
            page = ByteArray(64),
            pointerBytes = 8,
            parkTimeoutNs = -1L,
            joinTimeoutMs = 3_000,
            settleUs = 500,
            waiterCpu = 0,
            ownerCpu = 1,
            requeueCpu = 2,
            waiterNice = 0,
            ownerNice = 0,
            requeueNice = 0,
            policy = 0,
        )
        val result = port.attempt(params)
        port.close()
        words.close()

        Log.i(TAG, "n2-result=" + result)
        assertTrue(result is NativeAttemptResult.Ok)
        val answer = result as NativeAttemptResult.Ok
        assertTrue(answer.actorsStarted)
        assertEquals(0, answer.waitResult)
        assertTrue(answer.requeueResult >= 1)
    }

    private companion object {
        const val TAG = "N2Actor"
    }
}
