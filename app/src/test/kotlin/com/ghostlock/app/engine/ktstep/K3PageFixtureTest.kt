package com.ghostlock.app.engine.ktstep

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class K3PageFixtureTest {
    private fun page(): ByteArray = ByteArray(0x8000) { index -> (index and 0xFF).toByte() }

    private fun fakeForFixture(): FakeKernelSyscalls {
        val fake = FakeKernelSyscalls()
        fake.mmapAddresses.addAll(listOf(0x2000L, 0x1000L))
        fake.vmPages[0x2000L] = ByteArray(0x8000)
        fake.vmPages[0x1000L] = ByteArray(4096)
        fake.futexByOp[SyscallFutexWord.FUTEX_CMP_REQUEUE_PI] = SyscallResult.Ok(1L)
        return fake
    }

    @Test
    fun aFullPageWriteThenActorsIsFixtureVerified() {
        val fake = fakeForFixture()
        val result = K3PageFixture(fake, ProcessVmPageWriter(fake, 0)).run(page())

        assertTrue(result.pageWritten)
        assertTrue(result.readbackOk)
        assertTrue(result.fixtureSucceeded)
        assertNull(result.refusal)
        assertEquals(1, result.actors?.requeueResult)
        assertEquals(0, result.actors?.waitResult)
        assertEquals(1, fake.futexOps.count { it == SyscallFutexWord.FUTEX_CMP_REQUEUE_PI })
    }

    @Test
    fun aShortWriteStopsBeforeActors() {
        val fake = FakeKernelSyscalls()
        fake.mmapAddresses.add(0x2000L)
        fake.processVmWritevResult = SyscallResult.Ok(10L)
        val result = K3PageFixture(fake, ProcessVmPageWriter(fake, 0)).run(page())

        assertFalse(result.pageWritten)
        assertNull(result.actors)
        assertEquals("short-write:10", result.refusal)
        assertTrue(fake.futexOps.isEmpty())
    }

    @Test
    fun anEfaultStopsBeforeActors() {
        val fake = FakeKernelSyscalls()
        fake.mmapAddresses.add(0x2000L)
        fake.processVmWritevResult = SyscallResult.Errno(KernelErrno.EFAULT)
        val result = K3PageFixture(fake, ProcessVmPageWriter(fake, 0)).run(page())

        assertFalse(result.pageWritten)
        assertNull(result.actors)
        assertEquals("write:14", result.refusal)
        assertTrue(fake.futexOps.isEmpty())
    }

    @Test
    fun aReadbackMismatchStopsBeforeActors() {
        val fake = fakeForFixture()
        val writer = object : PageWriter {
            override val kind: PageWriterKind = PageWriterKind.Diagnostic
            override fun write(sink: Long, page: ByteArray): PageWriteResult = PageWriteResult.Written(page.size)
        }
        val result = K3PageFixture(fake, writer).run(page())

        assertTrue(result.pageWritten)
        assertFalse(result.readbackOk)
        assertEquals("readback", result.refusal)
        assertTrue(fake.futexOps.isEmpty())
    }

    @Test
    fun aTimedOutActorFailsTheFixtureAfterThePageWrite() {
        val fake = fakeForFixture()
        fake.futexSleepMs[SyscallFutexWord.FUTEX_WAIT_REQUEUE_PI] = 200L
        val result = K3PageFixture(
            fake,
            ProcessVmPageWriter(fake, 0),
            PiActorsConfig(joinTimeoutMs = 20L, requeueSettleMs = 0L),
        ).run(page())

        assertTrue(result.pageWritten)
        assertTrue(result.readbackOk)
        assertFalse(result.fixtureSucceeded)
        assertEquals("actors", result.refusal)
        assertFalse(result.actors!!.completed)
    }

    @Test
    fun aProductionWriterIsRefusedByTheFixture() {
        val fake = FakeKernelSyscalls()
        val writer = object : PageWriter {
            override val kind: PageWriterKind = PageWriterKind.Production
            override fun write(sink: Long, page: ByteArray): PageWriteResult = PageWriteResult.Written(page.size)
        }
        val result = K3PageFixture(fake, writer).run(page())

        assertFalse(result.fixtureSucceeded)
        assertEquals("writer-kind", result.refusal)
        assertTrue(fake.calls.isEmpty())
    }
}
