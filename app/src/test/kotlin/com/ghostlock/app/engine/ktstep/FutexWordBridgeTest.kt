package com.ghostlock.app.engine.ktstep

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class FutexWordBridgeTest {
    @Test
    fun aSyscallWordMapsValuesAndErrnos() {
        val fake = FakeKernelSyscalls()
        fake.script("futex", SyscallResult.Ok(0L), SyscallResult.Errno(11), SyscallResult.Ok(2L), SyscallResult.Errno(9))
        val target = SyscallFutexWord(fake, 0x2000L)
        val word = SyscallFutexWord(fake, 0x1000L)

        assertEquals(0, word.lockPi())
        assertEquals(-11, word.waitRequeuePi(target))
        assertEquals(2, word.cmpRequeuePi(target))
        assertEquals(-9, word.unlockPi())
        assertEquals(listOf("futex", "futex", "futex", "futex"), fake.calls)
    }

    @Test
    fun aMappedPageOwnsTwoWordsAndCloses() {
        val fake = FakeKernelSyscalls()
        fake.script("mmap", SyscallResult.Ok(0x1000L))
        fake.script("munmap", SyscallResult.Ok(0L))

        val made = KernelFutexWords.create(fake) as FutexWordsResult.Ok
        assertEquals(0x1000L, made.words.chain.address)
        assertEquals(0x1008L, made.words.target.address)
        assertEquals(0x1010L, made.words.wait.address)
        assertEquals(SyscallResult.Ok(0L), made.words.close())
        assertEquals(listOf("mmap", "getpid", "processVmWritev", "processVmWritev", "munmap"), fake.calls)
    }

    @Test
    fun mmapFailureAndZeroLengthAreNamed() {
        val failed = FakeKernelSyscalls().apply { script("mmap", SyscallResult.Errno(12)) }
        assertEquals(FutexWordsResult.Errno(12), KernelFutexWords.create(failed))
        assertEquals(FutexWordsResult.Errno(KernelErrno.EINVAL), KernelFutexWords.create(FakeKernelSyscalls(), 0L))
    }

    @Test
    fun aSpikeRunsPiAndClosesTheMapping() {
        val fake = FakeKernelSyscalls()
        fake.script("mmap", SyscallResult.Ok(0x1000L))
        fake.script("futex", SyscallResult.Ok(0L), SyscallResult.Ok(1L), SyscallResult.Ok(0L))
        fake.script("munmap", SyscallResult.Ok(0L))

        val result = PiSpike(fake).run()

        assertEquals(PiOutcome.Running, result.outcome)
        assertEquals(0x1000L, result.mapping)
        assertEquals(SyscallResult.Ok(0L), result.close)
        assertEquals(listOf("mmap", "getpid", "processVmWritev", "processVmWritev", "futex", "futex", "futex", "munmap"), fake.calls)
    }

    @Test
    fun aSpikeWithoutMappingIsRefused() {
        val fake = FakeKernelSyscalls().apply { script("mmap", SyscallResult.Errno(12)) }
        val result = PiSpike(fake).run()

        assertEquals("mmap:12", result.refusal)
        assertTrue(fake.calls == listOf("mmap"))
    }
}
