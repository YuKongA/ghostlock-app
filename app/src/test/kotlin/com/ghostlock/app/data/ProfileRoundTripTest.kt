package com.ghostlock.app.data

import com.ghostlock.app.data.route.MulticastConfig
import com.ghostlock.app.data.route.RouteKind
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
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
        /* Ancillary vr.ko guard: the gate rides meta, the layout is its own
         * section, and the symbol lives in offset like every other symbol. */
        "recommend_vr_guard" to 1L,
        "vr_guard.tracepoint_funcs" to 0x40L,
        "offset.vr_sys_exit_tp" to 0x21A1020L,
    )
    private val tcpValues = common + mapOf(
        "execution.routes.tcp_zerocopy.attempts" to 10L,
        "execution.routes.tcp_zerocopy.arm_sequence" to 1L,
        "execution.routes.tcp_zerocopy.post_receive_hold_iterations" to 2L,
    )
    private val selectValues = common + mapOf(
        "pselect_waiter_shift" to -2L,
        "execution.routes.select_stack.enter_delay_us" to 50000L,
        "execution.routes.select_stack.timeout_us" to 1000L,
    )
    private val multicastValues = common + mapOf(
        "mcast.waiter_off" to 264L,
        "mcast.buffer_size" to 512L,
        "mcast.task_offset" to 0x40L,
        "mcast.lock_offset" to 0x50L,
        "mcast.attempts" to 128L,
        "mcast.arm_sequence" to 16L,
        "mcast.arm_hold" to 20000L,
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
        val bytes = document("multicast_waiter", "select_stack", multicastValues).toBinary()
        val profile = Profile.fromBinary(bytes)!!

        assertEquals(RouteKind.MULTICAST_WAITER, profile.route)
        assertEquals(RouteKind.SELECT_STACK, profile.fallback)
        assertEquals("6.1.0-test", profile.release)
        assertEquals(6u, profile.kernelMajor)
        assertEquals(true, profile.supports(RouteKind.MULTICAST_WAITER))
        assertEquals(false, profile.supports(RouteKind.TCP_ZEROCOPY))
        assertEquals(true, profile.hasCompactWaiter())
        assertEquals(0x4000u, profile.mmStructStride(fallback = 1u))
        assertEquals(264, profile.multicastLayout().waiterOffset)
        val decoded = NativeProfileDocument.fromBinary(bytes)!!
        /* Poison/walk repetition rides the same route section. */
        val multicastConfig = decoded.routeConfig as MulticastConfig
        assertEquals(128u.toUByte(), multicastConfig.attempts)
        assertEquals(16u.toUByte(), multicastConfig.armSequence)
        assertEquals(20000u.toUShort(), multicastConfig.armHold)
        /* Consumer cadence rides its own execution section, not the multicast one. */
        assertEquals(1u, decoded.execution.consumerMaxCalls)
        assertEquals(1u, decoded.execution.consumerBurstCalls)

        assertArrayEquals(bytes, profile.toBinary())
    }

    @Test
    fun `vr guard round trip carries gate layout and symbol`() {
        val bytes = document("multicast_waiter", "none", multicastValues).toBinary()
        val decoded = NativeProfileDocument.fromBinary(bytes)!!

        assertEquals(1u, decoded.vrGuard)
        assertEquals(0x40u, decoded.vrGuardTracepointFuncs)
        assertEquals(0x21A1020uL, decoded.kernelOffset.vrSysExitTp)
        /* The layout is per-image: a profile without it must decode as absent so
         * the behavior stays fail-closed. */
        val withoutLayout = document(
            "multicast_waiter",
            "none",
            common - "vr_guard.tracepoint_funcs",
        ).toBinary()
        assertNull(NativeProfileDocument.fromBinary(withoutLayout)!!.vrGuardTracepointFuncs)
    }

    @Test
    fun `select round trip exposes waiter shift`() {
        val bytes = document("select_stack", null, selectValues).toBinary()
        val profile = Profile.fromBinary(bytes)!!
        assertEquals(RouteKind.SELECT_STACK, profile.route)
        assertEquals(-2, profile.selectStackLayout().waiterShift)
        assertArrayEquals(bytes, profile.toBinary())
    }

    @Test
    fun `patch safe mode lands on the meta entry`() {
        val original = document("multicast_waiter", null, multicastValues)
        val bytes = original.toBinary()
        val patched = NativeProfileDocument.patchSafeMode(bytes)!!
        val decoded = NativeProfileDocument.fromBinary(patched)!!
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
        assertArrayEquals(document("tcp_zerocopy", null, tcpValues).toBinary(), profile.toBinary())
    }
}
