package com.ghostlock.app.engine.ktstep

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class PiActorsKernelRouteTest {
    private fun pageInputs(): PayloadPageInputs = PayloadPageInputs(
        slots = PayloadSlots(1, 2, 3, 4, 5),
        lock = LockFields(0, 0, 0, 0),
        waiter = WaiterFields(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
        task = TaskFields(0, 0, 0, 0, 0, 0, 0, 0),
        values = PayloadValues(0, 0, 0),
        waiterWord = 0L,
        waiterTask = 0L,
        fakeTask = 0L,
        fakeLock = 0L,
        taskGroup = 0L,
        piTopTask = 0L,
        pointerBytes = 8,
    )

    private fun request(): WriteRequest = WriteRequest(0x1000L, WriteMode.Zero, true)

    @Test
    fun aRequeueNeverBecomesLandedWithoutPageWrite() {
        val fake = FakeKernelSyscalls()
        fake.script("mmap", SyscallResult.Ok(0x1000L))
        fake.futexByOp[SyscallFutexWord.FUTEX_CMP_REQUEUE_PI] = SyscallResult.Ok(1L)
        fake.script("munmap", SyscallResult.Ok(0L))

        val result = PiActorsKernelRoute(fake).attempt(request(), ByteArray(0x1000), pageInputs())

        assertTrue("result=$result calls=${fake.calls}", result is RouteResult.Indeterminate)
        assertTrue((result as RouteResult.Indeterminate).reason.contains("page-write-unimplemented"))
        assertEquals("munmap", fake.calls.last())
    }

    @Test
    fun mmapFailureIsRefused() {
        val fake = FakeKernelSyscalls().apply { script("mmap", SyscallResult.Errno(12)) }
        val result = PiActorsKernelRoute(fake).attempt(request(), ByteArray(0x1000), pageInputs())

        assertEquals(RouteResult.Refused("mmap:12"), result)
    }

    @Test
    fun zeroRequeueIsRefused() {
        val fake = FakeKernelSyscalls()
        fake.script("mmap", SyscallResult.Ok(0x1000L))
        fake.script("munmap", SyscallResult.Ok(0L))
        val result = PiActorsKernelRoute(fake).attempt(request(), ByteArray(0x1000), pageInputs())

        assertEquals(RouteResult.Refused("probe:requeue=0"), result)
    }
}
