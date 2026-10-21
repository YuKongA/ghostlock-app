package com.ghostlock.app.engine.ktstep

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Test

class ProductionBoundaryTest {
    @Test
    fun theProductionWriterAndProbeFailClosed() {
        val writer = KernelPageWriter()
        val probe = ProductionVictimProbe()

        assertEquals(PageWriterKind.Production, writer.kind)
        assertEquals(PageWriteResult.Errno(KernelErrno.ENOSYS), writer.write(0x1000L, ByteArray(32)))
        assertEquals(ProbeKind.Production, probe.kind)
        assertEquals(SyscallResult.Errno(KernelErrno.ENOSYS), probe.uid())
    }

    @Test
    fun theProductionRouteNeverLandsWhileTheWriterIsMissing() {
        val route = KernelPageRoute(KernelPageWriter())
        val page = ByteArray(0x8000)
        val result = route.attempt(WriteRequest(0x1000L, WriteMode.Zero, true), page, inputs())

        assertEquals(RouteKind.Production, route.kind)
        assertEquals(RouteResult.Refused("page:38"), result)
    }

    private fun inputs(): PayloadPageInputs = PayloadPageInputs(
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
}
