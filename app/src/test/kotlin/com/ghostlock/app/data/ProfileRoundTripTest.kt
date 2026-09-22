package com.ghostlock.app.data

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class ProfileRoundTripTest {
    private val values = mapOf(
        "kernel_major" to 6L,
        "compact_waiter" to 1L,
        "pselect_waiter_shift" to -2L,
        "kernel_phys_load" to 0x80000000L,
        "task_struct.prio" to 0x20L,
        "task_struct.cred" to 0x30L,
        "cred.copy_size" to 0x88L,
        "offset.init_task" to 0x1000L,
        "offset.mcast_fake_bss" to 0x2000L,
        "mcast.waiter_off" to 264L,
        "mcast.buffer_size" to 512L,
        "kernelsnitch.collisions" to 7L,
        "kernelsnitch.mm_struct_sz" to 0x4000L,
        "execution.recommended_cpus.main" to 0L,
        "execution.recommended_cpus.consumer" to 1L,
        "execution.stages.w1_attempts" to 3L,
        "execution.routes.tcp_zerocopy.arm_sequence" to 1L,
    )

    private fun document(route: String?, fallback: String?): NativeProfileDocument =
        NativeProfileDocument.from("6.1.0-test", route, fallback) { values[it] }

    @Test
    fun `route kind maps token and wire both ways`() {
        for (kind in RouteKind.values()) {
            assertEquals(kind, RouteKind.fromToken(kind.token))
            assertEquals(kind, RouteKind.fromWire(kind.wire))
        }
        assertNull(RouteKind.fromWire(0))
        assertNull(RouteKind.fromToken("unknown"))
        assertNull(RouteKind.fromToken(null))
    }

    @Test
    fun `binary decode fully restores the document`() {
        val original = document("tcp_zerocopy", "select_stack")
        val decoded = NativeProfileDocument.fromBinary(original.toBinary())
        assertEquals(original, decoded)
    }

    @Test
    fun `profile round trip is byte identical and exposes route semantics`() {
        val bytes = document("multicast_waiter", "select_stack").toBinary()
        val profile = Profile.fromBinary(bytes)!!

        assertEquals(RouteKind.MULTICAST_WAITER, profile.route)
        assertEquals(RouteKind.SELECT_STACK, profile.fallback)
        assertEquals("6.1.0-test", profile.release)
        assertEquals(6L, profile.kernelMajor)
        assertEquals(true, profile.supports(RouteKind.MULTICAST_WAITER))
        assertEquals(false, profile.supports(RouteKind.TCP_ZEROCOPY))
        assertEquals(true, profile.hasCompactWaiter())
        assertEquals(0x4000L, profile.mmStructStride(fallback = 1L))
        assertEquals(264L, profile.multicastLayout().waiterOffset)
        assertEquals(0x2000L, profile.multicastLayout().fakeBssImageOffset)
        assertEquals(-2L, profile.selectStackLayout().waiterShift)

        assertArrayEquals(bytes, profile.toBinary())
    }

    @Test
    fun `unresolved route is rejected on decode`() {
        val unresolved = document(route = null, fallback = null)
        assertNull(Profile.fromBinary(unresolved.toBinary()))
    }

    @Test
    fun `corrupt magic and truncated payload are rejected`() {
        val bytes = document("select_stack", null).toBinary()
        assertNull(Profile.fromBinary(bytes.copyOf().also { it[0] = 0 }))
        assertNull(Profile.fromBinary(bytes.copyOf(bytes.size - 1)))
    }

    @Test
    fun `fromValueMap builds the same authority as fromBinary`() {
        val profile = Profile.fromValueMap(
            release = "6.1.0-test",
            route = RouteKind.TCP_ZEROCOPY,
            fallbackTo = null,
            value = { values[it] },
        )!!
        assertEquals(RouteKind.TCP_ZEROCOPY, profile.route)
        assertNull(profile.fallback)
        assertArrayEquals(document("tcp_zerocopy", null).toBinary(), profile.toBinary())
    }
}
