package com.ghostlock.app

import android.util.Log
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.ghostlock.app.engine.ktstep.KernelFutexWords
import com.ghostlock.app.engine.ktstep.DiscoverProcessor
import com.ghostlock.app.engine.ktstep.NativeDiscover43499Port
import com.ghostlock.app.engine.ktstep.NativeDiscoverParams
import com.ghostlock.app.engine.ktstep.NativeDiscoverResult
import com.ghostlock.app.engine.ktstep.PayloadSlots
import com.ghostlock.app.engine.ktstep.NativeKernelSyscalls
import com.ghostlock.app.engine.ktstep.SyscallResult
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/** Discover 迁移的 AVD refusal 门禁：小 words 必须具名拒绝，不跑搜索。 */
@RunWith(AndroidJUnit4::class)
class DiscoverProbeInstrumentedTest {
    @Test
    fun aSmallScanRunsAndReturnsRawData() {
        val syscalls = NativeKernelSyscalls()
        val mapped = syscalls.mmap(
            64L * 1024L,
            KernelFutexWords.PROT_READ or KernelFutexWords.PROT_WRITE,
            KernelFutexWords.MAP_PRIVATE or KernelFutexWords.MAP_ANONYMOUS,
            -1,
            0L,
        )
        assertTrue(mapped is SyscallResult.Ok)
        val address = (mapped as SyscallResult.Ok).value
        val port = NativeDiscover43499Port()
        assertEquals(SyscallResult.Ok(1L), port.open())
        val result = port.discover(
            NativeDiscoverParams(
                wordsAddress = address,
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
        port.close()
        syscalls.munmap(address, 64L * 1024L)

        Log.i(TAG, "discover-run=" + result)
        assertTrue(result is NativeDiscoverResult.Ok)
        val decision = DiscoverProcessor(
            payloadOffset = 0xC000L,
            slots = PayloadSlots(0x0E80, 0x1180, 0x1280, 0x1080, 0x0F80),
        ).process(result, address + 0xC000L)
        Log.i(TAG, "discover-processor=" + decision)
    }

    @Test
    fun aSmallWordsMappingIsRefusedByName() {
        val syscalls = NativeKernelSyscalls()
        val mapped = syscalls.mmap(
            4096L,
            KernelFutexWords.PROT_READ or KernelFutexWords.PROT_WRITE,
            KernelFutexWords.MAP_PRIVATE or KernelFutexWords.MAP_ANONYMOUS,
            -1,
            0L,
        )
        assertTrue(mapped is SyscallResult.Ok)
        val address = (mapped as SyscallResult.Ok).value
        val port = NativeDiscover43499Port()
        assertEquals(SyscallResult.Ok(1L), port.open())
        val result = port.discover(
            NativeDiscoverParams(
                wordsAddress = address,
                wordsBytes = 1,
                collisions = 4,
                threadCount = 4,
                possibleCpus = 8,
                pageSize = 4096,
                mmStructSize = 0x500,
                mmSlabOrder = 2,
                headerBytes = 0x100,
                totalPokes = 64,
                wordBytes = 8,
                handleBytes = 8,
                wakeRounds = 1,
                roundTimeoutUs = 1000,
                rangeStart = 0xffffffc000000000uL.toLong(),
                rangeEnd = 0xffffffc000001000uL.toLong(),
                counterOffset = 0,
                sweepTags = false,
            ),
        )
        port.close()
        syscalls.munmap(address, 4096L)

        Log.i(TAG, "discover-result=" + result)
        assertTrue(result is NativeDiscoverResult.Ok)
        val answer = result as NativeDiscoverResult.Ok
        assertEquals(false, answer.ran)
        assertEquals(5, answer.refusal)
    }

    private companion object {
        const val TAG = "DiscoverProbe"
    }
}
