package com.ghostlock.app.engine.ktstep

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class ProcessVictimSessionTest {
    private class FakeVictim(private val block: Boolean = false) : VictimProcess {
        var closes = 0; private set
        override fun uid(): Int? { if (block) Thread.sleep(200); return 0 }
        override fun close() { closes += 1 }
    }

    @Test
    fun startsAndClosesExactlyOnce() {
        val fake = FakeVictim()
        val session = ProcessVictimSession(command = emptyList(), processFactory = { fake })
        assertTrue(session.startDetailed() is VictimStartResult.Ok)
        assertTrue(session.uid() is SyscallResult.Ok)
        session.close(); session.close()
        assertEquals(1, fake.closes)
    }

    @Test
    fun factoryFailureBecomesNamedStartFailure() {
        val session = ProcessVictimSession(command = emptyList(), processFactory = { error("no-process") })
        val result = session.startDetailed()
        assertTrue(result is VictimStartResult.Failed)
        assertTrue((result as VictimStartResult.Failed).reason.startsWith("victim-start:"))
    }

    @Test
    fun probeTimeoutKillsAndCloses() {
        val fake = FakeVictim(block = true)
        val session = ProcessVictimSession(command = emptyList(), processFactory = { fake }, probeTimeoutMs = 20)
        session.start()
        assertEquals(KernelErrno.ETIMEDOUT, (session.uid() as SyscallResult.Errno).code)
        assertEquals(1, fake.closes)
    }
}
