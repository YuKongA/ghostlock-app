package com.ghostlock.app.data

import com.ghostlock.app.data.profile.Glkv3Decoder
import com.ghostlock.app.data.profile.Glkv3Document
import com.ghostlock.app.data.profile.Glkv3Encoder
import com.ghostlock.app.data.profile.Glkv3Value
import com.ghostlock.app.data.profile.NativeProfileGlkv3Adapter
import com.ghostlock.app.data.route.RouteKind
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class ProfileRoundTripTest {
    /* Route-independent values plus per-route values. The logical document
     * carries only the resolved route's tuning (R6a dropped the fallback); the
     * GLKv3 adapter maps them onto the owner-qualified wire paths. */
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
        /* Ancillary vr.ko guard: the gate rides common, the layout is its own
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
        vals: Map<String, Long>,
    ): NativeProfileDocument =
        NativeProfileDocument.from("6.1.0-test", route, value = { vals[it] })

    private fun encoded(
        route: String?,
        vals: Map<String, Long>,
    ): ByteArray =
        Glkv3Encoder.encode(NativeProfileGlkv3Adapter.adapt(document(route, vals)))

    private fun entry(document: Glkv3Document, section: String, key: String): Glkv3Value =
        document.sections
            .first { it.name == section }
            .entries
            .first { it.key == key }
            .value

    @Test
    fun `route kind maps token and wire both ways`() {
        for (kind in RouteKind.entries) {
            assertEquals(kind, RouteKind.resolve(kind.token))
            assertEquals(kind, RouteKind.fromWire(kind.wire))
        }
        assertNull(RouteKind.fromWire(0u))
        assertNull(RouteKind.resolve("unknown"))
        assertNull(RouteKind.resolve(null))
    }

    @Test
    fun `v3 codec round trips the adapted document for every route`() {
        for ((route, vals) in listOf(
            "tcp_zerocopy" to tcpValues,
            "select_stack" to selectValues,
            "multicast_waiter" to multicastValues,
        )) {
            val adapted = NativeProfileGlkv3Adapter.adapt(document(route, vals))
            val bytes = Glkv3Encoder.encode(adapted)
            val decoded = requireNotNull(Glkv3Decoder.decode(bytes))
            assertEquals(adapted.release, decoded.release)
            assertEquals(adapted.terminal, decoded.terminal)
            assertEquals(adapted.backend, decoded.backend)
            assertEquals(adapted.route, decoded.route)
            /* Canonical writer: re-encoding the decoded document is byte-identical. */
            assertArrayEquals(bytes, Glkv3Encoder.encode(decoded))
        }
    }

    @Test
    fun `multicast round trip exposes route semantics`() {
        val decoded = requireNotNull(
            Glkv3Decoder.decode(encoded("multicast_waiter", multicastValues)),
        )
        val routeSection = "backend.cve_2026_43499.route.multicast_waiter"
        assertEquals("multicast_waiter", decoded.route)
        assertEquals(6uL, decoded.kernelMajor)
        assertEquals(
            Glkv3Value.Bool(true),
            entry(decoded, "backend.cve_2026_43499.kernel", "compact_waiter"),
        )
        assertEquals(
            Glkv3Value.UInt(0x4000u),
            entry(decoded, "backend.cve_2026_43499.kernel", "mm_struct_sz"),
        )
        assertEquals(Glkv3Value.UInt(264u), entry(decoded, routeSection, "waiter_off"))
        assertEquals(Glkv3Value.UInt(512u), entry(decoded, routeSection, "buffer_size"))
        /* Poison/walk repetition rides the same route section. */
        assertEquals(Glkv3Value.UInt(128u), entry(decoded, routeSection, "attempts"))
        assertEquals(Glkv3Value.UInt(16u), entry(decoded, routeSection, "arm_sequence"))
        assertEquals(Glkv3Value.UInt(20000u), entry(decoded, routeSection, "arm_hold"))
        /* Consumer cadence rides its own execution section, not the multicast one. */
        val consumer = "backend.cve_2026_43499.execution.consumer"
        assertEquals(Glkv3Value.UInt(1u), entry(decoded, consumer, "max_calls"))
        assertEquals(Glkv3Value.UInt(1u), entry(decoded, consumer, "burst_calls"))
    }

    @Test
    fun `patch safe mode lands on the common entry`() {
        val original = encoded("multicast_waiter", multicastValues)
        assertEquals(false, requireNotNull(Glkv3Decoder.decode(original)).safeMode)
        val patched = requireNotNull(NativeProfileDocument.patchSafeMode(original))
        val decoded = requireNotNull(Glkv3Decoder.decode(patched))
        assertEquals(true, decoded.safeMode)
        /* Original input is untouched. */
        assertEquals(false, requireNotNull(Glkv3Decoder.decode(original)).safeMode)
    }

    @Test
    fun `a non-glkv3 blob is not patchable`() {
        assertNull(NativeProfileDocument.patchSafeMode(byteArrayOf(0x21, 0x07, 0x00, 0x0D)))
        assertNull(NativeProfileDocument.patchSafeMode(ByteArray(0)))
    }

    @Test
    fun `unresolved route is rejected by the authority`() {
        val unresolved = document(route = null, vals = tcpValues)
        assertNull(Profile.fromNativeDocument(unresolved))
    }

    @Test
    fun `fromValueMap builds the same authority as the logical document`() {
        val profile = Profile.fromValueMap(
            release = "6.1.0-test",
            route = RouteKind.TCP_ZEROCOPY,
            value = { tcpValues[it] },
        )!!
        assertEquals(RouteKind.TCP_ZEROCOPY, profile.route)
        assertEquals("tcp_zerocopy", NativeProfileGlkv3Adapter.adapt(profile.document).route)
    }
}
