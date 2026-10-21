package com.ghostlock.app.engine.ktstep

import org.junit.Assert.assertEquals
import org.junit.Test

class KernelSyscallsTest {
    @Test
    fun rawResultsMapToOkOrErrno() {
        assertEquals(SyscallResult.Errno(38), SyscallResult.of(-38L))
        assertEquals(SyscallResult.Ok(0L), SyscallResult.of(0L))
        assertEquals(SyscallResult.Ok(123L), SyscallResult.of(123L))
    }

    @Test
    fun everyCallIsRecordedAndReturnsItsScriptedErrno() {
        val cases: List<Pair<String, (KernelSyscalls) -> SyscallResult>> = listOf(
            "futex" to { sys -> sys.futex(0L, 0, 0, -1L, 0L, 0) },
            "mmap" to { sys -> sys.mmap(0L, 0, 0, -1, 0L) },
            "munmap" to { sys -> sys.munmap(0L, 0L) },
            "memfdCreate" to { sys -> sys.memfdCreate("x", 0) },
            "socket" to { sys -> sys.socket(0, 0, 0) },
            "setsockopt" to { sys -> sys.setsockopt(0, 0, 0, ByteArray(0)) },
            "send" to { sys -> sys.send(0, ByteArray(0), 0) },
            "recv" to { sys -> sys.recv(0, ByteArray(0), 0) },
            "close" to { sys -> sys.close(0) },
            "read" to { sys -> sys.read(0, ByteArray(0)) },
            "write" to { sys -> sys.write(0, ByteArray(0)) },
            "perfEventOpen" to { sys -> sys.perfEventOpen(ByteArray(0), 0, 0, -1, 0L) },
            "ioctl" to { sys -> sys.ioctl(0, 0L, 0L) },
            "gettid" to { sys -> sys.gettid() },
            "getpid" to { sys -> sys.getpid() },
            "processVmWritev" to { sys -> sys.processVmWritev(0, 0L, ByteArray(0)) },
            "processVmReadv" to { sys -> sys.processVmReadv(0, 0L, ByteArray(0)) },
            "usleep" to { sys -> sys.usleep(0L) },
            "clockGettimeNs" to { sys -> sys.clockGettimeNs(0) },
            "pipe2" to { sys -> sys.pipe2(IntArray(2), 0) },
            "schedSetaffinity" to { sys -> sys.schedSetaffinity(0, 0L) },
            "schedSetattr" to { sys -> sys.schedSetattr(0, ByteArray(0)) },
            "schedGetcpu" to { sys -> sys.schedGetcpu() },
            "rseq" to { sys -> sys.rseq(ByteArray(0), 0, 0) },
        )

        for ((name, invoke) in cases) {
            val fake = FakeKernelSyscalls()
            fake.script(name, SyscallResult.Errno(KernelErrno.EPERM))
            assertEquals(name, SyscallResult.Errno(KernelErrno.EPERM), invoke(fake))
            assertEquals(name, listOf(name), fake.calls)
        }
    }

    @Test
    fun nativeWithoutLibraryFailsClosed() {
        assertEquals(
            SyscallResult.Errno(KernelErrno.ENOSYS),
            NativeKernelSyscalls().gettid(),
        )
    }
}
