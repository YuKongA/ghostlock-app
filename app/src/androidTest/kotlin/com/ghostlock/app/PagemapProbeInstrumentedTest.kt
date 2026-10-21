package com.ghostlock.app

import android.util.Log
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.ghostlock.app.engine.ktstep.KernelErrno
import com.ghostlock.app.engine.ktstep.KernelFutexWords
import com.ghostlock.app.engine.ktstep.NativeKernelSyscalls
import com.ghostlock.app.engine.ktstep.SyscallResult
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File
import java.io.RandomAccessFile
import java.nio.ByteBuffer
import java.nio.ByteOrder

/** N3 取址探针：只记录 pagemap/kallsyms 可见性，不写内核。 */
@RunWith(AndroidJUnit4::class)
class PagemapProbeInstrumentedTest {
    @Test
    fun theAppCanSeeItsOwnPfn() {
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
        val entry = readPagemap(address)
        val pfn = entry and ((1L shl 55) - 1L)
        Log.i(TAG, "pagemap-address=0x" + address.toString(16) + " entry=0x" + entry.toString(16) + " pfn=" + pfn)
        Log.i(TAG, "kallsyms-init-task=" + grepKallsyms("init_task"))
        Log.i(TAG, "kallsyms-page-offset=" + grepKallsyms("page_offset_base"))
        syscalls.munmap(address, 4096L)
    }

    private fun readPagemap(address: Long): Long {
        val offset = (address / 4096L) * 8L
        return try {
            RandomAccessFile("/proc/self/pagemap", "r").use { file ->
                file.seek(offset)
                val bytes = ByteArray(8)
                file.readFully(bytes)
                ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN).long
            }
        } catch (failed: Exception) {
            Log.i(TAG, "pagemap-error=" + failed)
            0L
        }
    }

    private fun grepKallsyms(symbol: String): String {
        return try {
            File("/proc/kallsyms").readLines().firstOrNull { it.endsWith(" " + symbol) } ?: "missing"
        } catch (failed: Exception) {
            "error:" + failed
        }
    }

    private companion object {
        const val TAG = "PagemapProbe"
    }
}
