package com.ghostlock.app.engine.ktstep

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class ProcessVictimProbeTest {
    @Test
    fun aKotlinChildAnswersUidTwice() {
        val victim = ProcessVictim(listOf("/bin/sh", "-c", "while read _; do id -u; done"))
        victim.use {
            val probe = ProcessVictimProbe(it)
            val first = probe.uid()
            val second = probe.uid()

            assertEquals(ProbeKind.Production, probe.kind)
            assertTrue(first is SyscallResult.Ok)
            assertEquals(first, second)
            assertTrue((first as SyscallResult.Ok).value > 0L)
        }
    }
}
