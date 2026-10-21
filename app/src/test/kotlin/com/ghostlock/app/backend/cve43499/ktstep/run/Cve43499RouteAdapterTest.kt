package com.ghostlock.app.backend.cve43499.ktstep.run

import com.ghostlock.app.engine.ktstep.PayloadPageInputs
import com.ghostlock.app.engine.ktstep.PayloadSlots
import com.ghostlock.app.engine.ktstep.LockFields
import com.ghostlock.app.engine.ktstep.WaiterFields
import com.ghostlock.app.engine.ktstep.TaskFields
import com.ghostlock.app.engine.ktstep.PayloadValues
import com.ghostlock.app.engine.ktstep.RouteResult
import com.ghostlock.app.engine.ktstep.WriteMode
import com.ghostlock.app.engine.ktstep.WriteRequest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class Cve43499RouteAdapterTest {
    private class FakePort : Cve43499RoutePort {
        override val route = Cve43499RouteId.TcpZerocopy
        var calls = 0
        override fun attempt(request: WriteRequest, page: ByteArray, inputs: PayloadPageInputs, params: ByteArray): RouteResult {
            calls += 1
            return RouteResult.Landed()
        }
        override fun close() = Unit
    }

    @Test
    fun adapterDelegatesToSelectedPort() {
        val port = FakePort()
        val inputs = PayloadPageInputs(
            PayloadSlots(0,0,0,0,0), LockFields(0,0,0,0),
            WaiterFields(0,0,0,0,0,0,0,0,0,0,0,0), TaskFields(0,0,0,0,0,0,0,0),
            PayloadValues(0,0,0), 0, 0, 0, 0, 0, 0, 8,
        )
        val result = Cve43499RouteAdapter(port).attempt(WriteRequest(1, WriteMode.Zero, true), ByteArray(1), inputs)
        assertTrue(result is RouteResult.Landed)
        assertEquals(1, port.calls)
    }
}
