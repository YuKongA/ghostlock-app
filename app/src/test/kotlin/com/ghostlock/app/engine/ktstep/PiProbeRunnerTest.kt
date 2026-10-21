package com.ghostlock.app.engine.ktstep

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class PiProbeRunnerTest {
    @Test
    fun aRequeueSuccessIsReportedButNeverRoot() {
        val fake = FakeKernelSyscalls()
        fake.script("mmap", SyscallResult.Ok(0x1000L))
        fake.futexByOp[SyscallFutexWord.FUTEX_CMP_REQUEUE_PI] = SyscallResult.Ok(1L)
        fake.script("munmap", SyscallResult.Ok(0L))

        val report = PiProbeRunner(fake).run()
        val lines = report.logLines()

        assertEquals(1, report.actors?.requeueResult)
        assertTrue(report.actors!!.completed)
        assertTrue(lines.contains("k0b-requeue=1"))
        assertTrue(lines.contains("k0b-page-write=false"))
        assertTrue(lines.contains("k0b-root=false"))
        assertFalse(report.root)
    }

    @Test
    fun mmapFailureIsNamed() {
        val fake = FakeKernelSyscalls().apply { script("mmap", SyscallResult.Errno(12)) }
        val report = PiProbeRunner(fake).run()

        assertEquals("mmap:12", report.refusal)
        assertTrue(report.logLines().contains("k0b-refusal=mmap:12"))
        assertTrue(report.logLines().contains("k0b-root=false"))
    }

    @Test
    fun closeFailureIsVisible() {
        val fake = FakeKernelSyscalls()
        fake.script("mmap", SyscallResult.Ok(0x1000L))
        fake.script("munmap", SyscallResult.Errno(22))
        val report = PiProbeRunner(fake).run()

        assertEquals(SyscallResult.Errno(22), report.close)
        assertTrue(report.logLines().contains("k0b-close=Errno(code=22)"))
    }
}
