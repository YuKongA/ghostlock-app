package com.ghostlock.app.engine.kernelmemory

import com.ghostlock.app.core.steps.StepId
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class PortOpsInvokerTest {
    private fun steps(): Map<StepId, KernelStep> = mapOf(
        StepId.W1 to KernelStep(StepId.W1, 1, 0L, 0x1000L, 0x1000L, 0L),
        StepId.W2 to KernelStep(StepId.W2, 1, 0L, 0x2000L, 0x2000L, 0xCAFEL),
        StepId.W3 to KernelStep(StepId.W3, 1, 0L, 0x3000L, 0x3000L, 0L),
    )

    @Test
    fun aWriteWordMapsToTheStepByAddress() {
        val port = FakeBackend43499Port()
        val invoker = PortOpsInvoker(port, steps())

        val reply = invoker.invoke(
            "write_word",
            mapOf("address" to 0x2000uL, "value" to 0xCAFEuL, "form" to 3uL, "offset" to 0uL),
        )

        assertTrue(reply.ok)
        assertEquals(1, port.writes.size)
        assertEquals(StepId.W2, port.writes[0].step)
        assertEquals(0xCAFEuL.toLong(), port.writes[0].value)
        assertEquals(3, port.writes[0].form)
    }

    @Test
    fun unknownAddressIsRefusedWithoutCallingThePort() {
        val port = FakeBackend43499Port()
        val invoker = PortOpsInvoker(port, steps())

        val reply = invoker.invoke("write_word", mapOf("address" to 0x9999uL, "form" to 1uL))

        assertFalse(reply.ok)
        assertEquals("bad-parameters", reply.status)
        assertTrue(port.writes.isEmpty())
    }

    @Test
    fun missingFormIsRefusedWithoutCallingThePort() {
        val port = FakeBackend43499Port()
        val invoker = PortOpsInvoker(port, steps())

        val reply = invoker.invoke("write_word", mapOf("address" to 0x1000uL))

        assertFalse(reply.ok)
        assertEquals("bad-parameters", reply.status)
        assertTrue(port.writes.isEmpty())
    }

    @Test
    fun inFlightMapsToIndeterminate() {
        val port = FakeBackend43499Port().apply {
            writeHandler = { PortResult.Refused(PortRefusal(PortCode.IN_FLIGHT, "in-flight")) }
        }
        val invoker = PortOpsInvoker(port, steps())

        val reply = invoker.invoke(
            "write_word",
            mapOf("address" to 0x1000uL, "form" to 1uL, "offset" to 0uL),
        )

        assertEquals("indeterminate", reply.status)
        assertEquals("in-flight:in-flight", reply.refusal)
    }

    @Test(expected = IllegalArgumentException::class)
    fun duplicateAddressesAreRejectedAtConstruction() {
        val broken = steps().toMutableMap()
        broken[StepId.W2] = broken.getValue(StepId.W2).copy(address = 0x1000L)
        PortOpsInvoker(FakeBackend43499Port(), broken)
    }
}
