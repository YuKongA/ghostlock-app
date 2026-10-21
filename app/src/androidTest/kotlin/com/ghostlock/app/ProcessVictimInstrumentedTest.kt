package com.ghostlock.app

import androidx.test.ext.junit.runners.AndroidJUnit4
import com.ghostlock.app.engine.ktstep.KtStepPlan
import com.ghostlock.app.engine.ktstep.ProcessVictimSession
import com.ghostlock.app.engine.ktstep.SyscallResult
import com.ghostlock.app.engine.ktstep.VictimStartResult
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/** AVD victim lifecycle: real /system/bin/sh, no route/kernel call. */
@RunWith(AndroidJUnit4::class)
class ProcessVictimInstrumentedTest {
    @Test
    fun realVictimStartsProbesAndCloses() {
        val session = ProcessVictimSession()
        assertTrue(session.startDetailed() is VictimStartResult.Ok)
        assertTrue(session.uid() is SyscallResult.Ok)
        session.close()
    }
}
