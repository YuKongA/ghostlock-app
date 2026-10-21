package com.ghostlock.app

import android.util.Log
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.ghostlock.app.engine.ktstep.KernelFutexWords
import com.ghostlock.app.engine.ktstep.NativeAttemptParams
import com.ghostlock.app.engine.ktstep.NativeAttemptResult
import com.ghostlock.app.engine.ktstep.NativeJniAttemptPort
import com.ghostlock.app.engine.ktstep.NativeKernelSyscalls
import com.ghostlock.app.engine.ktstep.SyscallResult
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/** N3 发送通道：C++ 把 Kotlin 页写进 fd（socket/pipe），actor 再跑。 */
@RunWith(AndroidJUnit4::class)
class N3PipeInstrumentedTest {
    @Test
    fun thePageGoesThroughTheNativeFd() {
        val syscalls = NativeKernelSyscalls()
        val fds = IntArray(2)
        assertTrue(syscalls.pipe2(fds, 0) is SyscallResult.Ok)
        val page = ByteArray(0x4000) { (it and 0xFF).toByte() }
        val words = when (val made = KernelFutexWords.create(syscalls, waitTimeoutNs = -1L)) {
            is com.ghostlock.app.engine.ktstep.FutexWordsResult.Ok -> made.words
            is com.ghostlock.app.engine.ktstep.FutexWordsResult.Errno -> error("words:" + made.code)
        }
        val port = NativeJniAttemptPort()
        assertEquals(SyscallResult.Ok(1L), port.open())
        val result = port.attempt(
            NativeAttemptParams(
                chainAddress = words.chain.address,
                waitAddress = words.wait.address,
                targetAddress = words.target.address,
                pageSinkAddress = 1L,
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
                pageFd = fds[1],
            ),
        )
        val back = ByteArray(page.size)
        val read = syscalls.read(fds[0], back)
        port.close()
        words.close()
        syscalls.close(fds[0])
        syscalls.close(fds[1])

        Log.i(TAG, "n3-pipe=" + result + " read=" + read)
        assertTrue(result is NativeAttemptResult.Ok)
        assertEquals(page.size, (result as NativeAttemptResult.Ok).pageBytesWritten)
        assertTrue(read is SyscallResult.Ok)
        assertEquals(page.size.toLong(), (read as SyscallResult.Ok).value)
        assertTrue(page.contentEquals(back))
    }

    @Test
    fun aBadPageFdDoesNotStartActors() {
        val syscalls = NativeKernelSyscalls()
        val fds = IntArray(2)
        assertTrue(syscalls.pipe2(fds, 0) is SyscallResult.Ok)
        val words = when (val made = KernelFutexWords.create(syscalls, waitTimeoutNs = -1L)) {
            is com.ghostlock.app.engine.ktstep.FutexWordsResult.Ok -> made.words
            is com.ghostlock.app.engine.ktstep.FutexWordsResult.Errno -> error("words:" + made.code)
        }
        val port = NativeJniAttemptPort()
        assertEquals(SyscallResult.Ok(1L), port.open())
        val result = port.attempt(
            NativeAttemptParams(
                chainAddress = words.chain.address,
                waitAddress = words.wait.address,
                targetAddress = words.target.address,
                pageSinkAddress = 1L,
                chainValue = 0,
                waitValue = 0,
                targetValue = 0,
                page = ByteArray(0x4000),
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
                pageFd = fds[0],
            ),
        )
        port.close()
        words.close()
        syscalls.close(fds[0])
        syscalls.close(fds[1])

        Log.i(TAG, "n3-bad-fd=" + result)
        assertTrue(result is NativeAttemptResult.Ok)
        val answer = result as NativeAttemptResult.Ok
        assertEquals(0, answer.pageBytesWritten)
        assertTrue(answer.pageErrno != 0)
        assertTrue(!answer.actorsStarted)
    }

    private companion object {
        const val TAG = "N3Pipe"
    }
}
