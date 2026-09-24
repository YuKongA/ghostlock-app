package com.ghostlock.app.data

import com.ghostlock.app.data.route.RouteKind
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Test

class ProfileRoundTripTest {
    /* Route-independent values plus per-route values. v2 only carries the
     * route section of the document's own route. */
    private val common = mapOf(
        "kernel_major" to 6L,
        "compact_waiter" to 1L,
        "kernel_phys_load" to 0x80000000L,
        "task_struct.prio" to 0x20L,
        "task_struct.cred" to 0x30L,
        "cred.copy_size" to 0x88L,
        "offset.init_task" to 0x1000L,
        "kernelsnitch.collisions" to 7L,
        "kernelsnitch.mm_struct_sz" to 0x4000L,
        "execution.recommended_cpus.main" to 0L,
        "execution.recommended_cpus.consumer" to 1L,
        "execution.stages.w1_attempts" to 3L,
        /* Drives the shared consumer thread, so it must survive on every
         * route (the multicast primitive uses the same PI consumer). */
        "execution.routes.select_stack.consumer_max_calls" to 1L,
        "execution.routes.select_stack.consumer_burst_calls" to 1L,
    )
    private val tcpValues = common + mapOf(
        "execution.routes.tcp_zerocopy.arm_sequence" to 1L,
    )
    private val selectValues = common + mapOf(
        "pselect_waiter_shift" to -2L,
        "execution.routes.select_stack.enter_delay_us" to 50000L,
    )
    private val multicastValues = common + mapOf(
        "offset.mcast_fake_bss" to 0x2000L,
        "mcast.waiter_off" to 264L,
        "mcast.buffer_size" to 512L,
        "execution.routes.multicast_waiter.ready_timeout_ms" to 1234L,
    )

    private fun document(
        route: String?,
        fallback: String?,
        vals: Map<String, Long>,
    ): NativeProfileDocument =
        NativeProfileDocument.from("6.1.0-test", route, fallback) { vals[it] }

    @Test
    fun `route kind maps token and wire both ways`() {
        for (kind in RouteKind.values()) {
            assertEquals(kind, RouteKind.fromToken(kind.token))
            assertEquals(kind, RouteKind.fromWire(kind.wire))
        }
        assertNull(RouteKind.fromWire(0u))
        assertNull(RouteKind.fromToken("unknown"))
        assertNull(RouteKind.fromToken(null))
    }

    @Test
    fun `binary decode fully restores the document`() {
        for ((route, vals) in listOf(
            "tcp_zerocopy" to tcpValues,
            "select_stack" to selectValues,
            "multicast_waiter" to multicastValues,
        )) {
            val original = document(route, "select_stack", vals)
            val decoded = NativeProfileDocument.fromBinary(original.toBinary())
            assertEquals(original, decoded)
        }
    }

    @Test
    fun `multicast round trip exposes route semantics`() {
        val bytes = document("multicast_waiter", "select_stack", multicastValues).toBinaryV3()
        val profile = Profile.fromBinary(bytes)!!

        assertEquals(RouteKind.MULTICAST_WAITER, profile.route)
        assertEquals(RouteKind.SELECT_STACK, profile.fallback)
        assertEquals("6.1.0-test", profile.release)
        assertEquals(6u, profile.kernelMajor)
        assertEquals(true, profile.supports(RouteKind.MULTICAST_WAITER))
        assertEquals(false, profile.supports(RouteKind.TCP_ZEROCOPY))
        assertEquals(true, profile.hasCompactWaiter())
        assertEquals(0x4000u, profile.mmStructStride(fallback = 1u))
        assertEquals(264uL, profile.multicastLayout().waiterOffset)
        assertEquals(0x2000uL, profile.multicastLayout().fakeBssImageOffset)
        /* Consumer cadence rides the common slot, not the multicast section. */
        val decoded = NativeProfileDocument.fromBinary(bytes)!!
        assertEquals(1u, decoded.execution.consumerMaxCalls)
        assertEquals(1u, decoded.execution.consumerBurstCalls)

        assertArrayEquals(bytes, profile.toBinary())
    }

    @Test
    fun `select round trip exposes waiter shift`() {
        val bytes = document("select_stack", null, selectValues).toBinaryV3()
        val profile = Profile.fromBinary(bytes)!!
        assertEquals(RouteKind.SELECT_STACK, profile.route)
        assertEquals(-2L, profile.selectStackLayout().waiterShift)
        assertArrayEquals(bytes, profile.toBinary())
    }

    @Test
    fun `safe mode patch targets the trailing common slot`() {
        val original = document("multicast_waiter", null, multicastValues)
        val bytes = original.toBinary()
        val offset = NativeProfileDocument.safeModeOffset(bytes)!!
        bytes[offset] = 1

        val decoded = NativeProfileDocument.fromBinary(bytes)!!
        assertEquals(1u, decoded.safeMode)
        assertEquals(original.copy(safeMode = 1u), decoded)
    }

    @Test
    fun `unresolved route is rejected on decode`() {
        val unresolved = document(route = null, fallback = null, vals = tcpValues)
        assertNull(Profile.fromBinary(unresolved.toBinary()))
    }

    @Test
    fun `corrupt magic and truncated payload are rejected`() {
        val bytes = document("select_stack", null, selectValues).toBinary()
        assertNull(Profile.fromBinary(bytes.copyOf().also { it[0] = 0 }))
        assertNull(Profile.fromBinary(bytes.copyOf(bytes.size - 1)))
    }

    @Test
    fun `fromValueMap builds the same authority as fromBinary`() {
        val profile = Profile.fromValueMap(
            release = "6.1.0-test",
            route = RouteKind.TCP_ZEROCOPY,
            fallbackTo = null,
            value = { tcpValues[it] },
        )!!
        assertEquals(RouteKind.TCP_ZEROCOPY, profile.route)
        assertNull(profile.fallback)
        assertArrayEquals(document("tcp_zerocopy", null, tcpValues).toBinaryV3(), profile.toBinary())
    }

    @Test
    fun `v3 decode rejects unknown component ids`() {
        val bytes = document("select_stack", null, selectValues).toBinaryV3()
        fun patched(frontend: Int, backend: Int, middleware: Int): ByteArray {
            val copy = bytes.copyOf()
            copy[6] = frontend.toByte(); copy[7] = (frontend shr 8).toByte()
            copy[8] = backend.toByte(); copy[9] = (backend shr 8).toByte()
            copy[10] = middleware.toByte(); copy[11] = (middleware shr 8).toByte()
            return copy
        }
        assertNotNull(NativeProfileDocument.fromBinary(patched(1, 1, 2)))
        /* Known-but-unavailable ids decode; the orchestrator rejects them. */
        assertNotNull(NativeProfileDocument.fromBinary(patched(2, 1, 2)))
        assertNotNull(NativeProfileDocument.fromBinary(patched(1, 2, 2)))
        assertNull(NativeProfileDocument.fromBinary(patched(9, 1, 2)))
        assertNull(NativeProfileDocument.fromBinary(patched(1, 9, 2)))
        assertNull(NativeProfileDocument.fromBinary(patched(1, 1, 99)))
    }

    @Test
    fun `v3 safe mode patch lands on the core slot`() {
        val original = document("select_stack", null, selectValues)
        val bytes = original.toBinaryV3()
        val offset = NativeProfileDocument.safeModeOffset(bytes)!!
        bytes[offset] = 1
        val decoded = NativeProfileDocument.fromBinary(bytes)!!
        assertEquals(1u, decoded.safeMode)
        assertEquals(original.copy(safeMode = 1u), decoded)
    }
}
