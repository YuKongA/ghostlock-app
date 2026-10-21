package com.ghostlock.app.engine.ktstep

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class NativeAttemptKernelRouteTest {
    private fun template() = NativeAttemptParams(
        chainAddress = 0x1000, waitAddress = 0x2000, targetAddress = 0x3000,
        pageSinkAddress = 0x4000, chainValue = 1, waitValue = 2, targetValue = 3,
        page = ByteArray(4), pointerBytes = 8, parkTimeoutNs = -1L, joinTimeoutMs = 100,
        settleUs = 0, waiterCpu = 0, ownerCpu = 1, requeueCpu = 2,
        waiterNice = 0, ownerNice = 0, requeueNice = 0, policy = 0,
    )

    private fun port(result: NativeAttemptResult) = object : NativeAttemptPort {
        override fun open() = SyscallResult.Ok(0)
        override fun attempt(params: NativeAttemptParams) = result
        override fun spawnChild(params: NativeChildParams) = SyscallResult.Errno(KernelErrno.ENOSYS)
        override fun childCommand(cmd: Int, payload: ByteArray) = SyscallResult.Errno(KernelErrno.ENOSYS)
        override fun childFinish(action: Int) = SyscallResult.Errno(KernelErrno.ENOSYS)
        override fun close() = SyscallResult.Ok(0)
    }

    private fun ok(requeue: Int = 1, wait: Int = 0) = NativeAttemptResult.Ok(
        waiterResult = 0, waitResult = wait, ownerResult = 0, requeueResult = requeue,
        actorsStarted = true, pageStage = 1, pageErrno = 0, pageBytesWritten = 4,
        joinTimedOut = false, elapsedNs = 1,
    )

    private fun attempt(result: NativeAttemptResult): RouteResult {
        val route = NativeAttemptKernelRoute(port(result), template())
        return route.attempt(WriteRequest(0x3000, WriteMode.Zero, true), ByteArray(4), PayloadPageInputs(
            PayloadSlots(0,0,0,0,0), LockFields(0,0,0,0),
            WaiterFields(0,0,0,0,0,0,0,0,0,0,0,0), TaskFields(0,0,0,0,0,0,0,0),
            PayloadValues(0,0,0), 0, 0, 0, 0, 0, 0, 8,
        ))
    }

    @Test
    fun completePageAndActorsLand() {
        assertEquals(RouteResult.Landed(), attempt(ok()))
    }

    @Test
    fun pageAndActorFailuresAreNamed() {
        assertTrue(attempt(ok(requeue = 0)) is RouteResult.Indeterminate)
        assertTrue(attempt(ok(wait = 1)) is RouteResult.Indeterminate)
        assertTrue(attempt(NativeAttemptResult.Errno(38)) is RouteResult.Refused)
    }
}
