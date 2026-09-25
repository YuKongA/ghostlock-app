package com.ghostlock.app.data

import com.ghostlock.app.data.route.TcpConfig
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test

/**
 * White-box tests for the v2 transport layout shared with native
 * (`profile/binary.cpp`): exact header bytes, the 68 common slot indices, the
 * per-route section keys, and rejection of malformed documents.
 */
class NativeProfileDocumentTest {
    private val release = "6.1.0-layout-test"

    private fun doc(route: String?, values: Map<String, Long> = emptyMap()) =
        NativeProfileDocument.from(release, route, null) { values[it] }

    private fun readU16(bytes: ByteArray, offset: Int): Int =
        (bytes[offset].toInt() and 0xff) or ((bytes[offset + 1].toInt() and 0xff) shl 8)

    private fun readU32(bytes: ByteArray, offset: Int): Long =
        (0 until 4).fold(0L) { acc, i ->
            acc or ((bytes[offset + i].toLong() and 0xff) shl (8 * i))
        }

    private fun readLong(bytes: ByteArray, offset: Int): Long =
        (0 until 8).fold(0L) { acc, i ->
            acc or ((bytes[offset + i].toLong() and 0xff) shl (8 * i))
        }

    private fun releaseLength(bytes: ByteArray): Int = readU16(bytes, 10)

    private fun commonBase(bytes: ByteArray): Int = 12 + releaseLength(bytes)

    private fun routeEntries(bytes: ByteArray): List<Pair<String, Long>> {
        var p = commonBase(bytes) + 68 * 8
        val count = bytes[p++].toInt() and 0xff
        val entries = mutableListOf<Pair<String, Long>>()
        repeat(count) {
            val keyLength = bytes[p++].toInt() and 0xff
            val key = String(bytes, p, keyLength, Charsets.UTF_8)
            p += keyLength
            entries += key to readLong(bytes, p)
            p += 8
        }
        return entries
    }

    @Test
    fun `header uses the agreed magic and version`() {
        val bytes = doc("select_stack").toBinary()
        assertEquals(0x0D000721L, NativeProfileDocument.Magic.toLong())
        assertEquals(2, NativeProfileDocument.Version.toInt())
        assertEquals(0x0D000721L, readU32(bytes, 0))
        assertEquals(2, readU16(bytes, 4))
        assertEquals(2, bytes[6].toInt() and 0xff) // select_stack
        assertEquals(0, bytes[7].toInt() and 0xff) // kernel_major
        assertEquals(0, bytes[8].toInt() and 0xff) // recommend_shizuku
        assertEquals(0, bytes[9].toInt() and 0xff) // fallback_route
        assertEquals(release.length, releaseLength(bytes))
        assertEquals(release, String(bytes, 12, releaseLength(bytes), Charsets.UTF_8))
    }

    @Test
    fun `route section carries exactly the route's keys`() {
        assertEquals(
            listOf("tcp_attempts", "tcp_arm_sequence", "tcp_post_receive_hold_iterations"),
            routeEntries(doc("tcp_zerocopy").toBinary()).map { it.first },
        )
        assertEquals(
            listOf("pselect_waiter_shift", "select_enter_delay_us", "select_timeout_us"),
            routeEntries(doc("select_stack").toBinary()).map { it.first },
        )
        assertEquals(
            listOf(
                "mcast_waiter_off", "mcast_buffer_size", "mcast_task_offset",
                "mcast_lock_offset", "mcast_fake_lock_offset", "mcast_fake_task_offset",
                "mcast_lock_slots_offset", "mcast_lock_slot_count", "mcast_lock_slot_stride",
                "off_mcast_fake_bss", "multicast_resident", "multicast_ready_timeout_ms",
                "multicast_post_requeue_settle_us", "multicast_post_adjust_settle_us",
            ),
            routeEntries(doc("multicast_waiter").toBinary()).map { it.first },
        )
        /* An unresolved route emits no section at all. */
        val unresolved = doc(route = null).toBinary()
        assertEquals(0, unresolved[6].toInt() and 0xff)
        assertTrue(routeEntries(unresolved).isEmpty())
    }

    @Test
    fun `document size is header plus 68 common slots plus the route section`() {
        val bytes = doc("select_stack").toBinary()
        val routeBytes = routeEntries(bytes).sumOf { 1 + it.first.toByteArray().size + 8 } + 1
        assertEquals(12 + release.length + 68 * 8 + routeBytes, bytes.size)
    }

    @Test
    fun `common slots keep their agreed indices`() {
        val bytes = doc(
            "select_stack",
            mapOf(
                "task_struct.prio" to 0x20L,
                "cred.copy_size" to 0x88L,
                "offset.init_task" to 0x1000L,
                "execution.routes.select_stack.consumer_max_calls" to 1L,
                "execution.routes.select_stack.consumer_burst_calls" to 1L,
            ),
        ).toBinary()
        val base = commonBase(bytes)
        assertEquals(0x20L, readLong(bytes, base + 0 * 8)) // task_prio
        assertEquals(0x88L, readLong(bytes, base + 15 * 8)) // cred_copy_size
        assertEquals(0x1000L, readLong(bytes, base + 30 * 8)) // off_init_task
        assertEquals(1L, readLong(bytes, base + 65 * 8)) // consumer_max_calls
        assertEquals(1L, readLong(bytes, base + 66 * 8)) // consumer_burst_calls
        assertEquals(0L, readLong(bytes, base + 67 * 8)) // safe_mode (patched later)
        assertEquals(base + 67 * 8, NativeProfileDocument.safeModeOffset(bytes))
    }

    @Test
    fun `v3 carries route done timeout as a compatible named option`() {
        val document = doc(
            "select_stack",
            mapOf("execution.race.route_done_timeout_ms" to 300000L),
        )
        val decoded = NativeProfileDocument.fromBinary(document.toBinaryV3())!!
        assertEquals(300000u, decoded.execution.raceRouteDoneTimeoutMs)

        /* v2 has no option section; Native applies its runtime default. */
        val legacy = NativeProfileDocument.fromBinary(document.toBinary())!!
        assertEquals(0u, legacy.execution.raceRouteDoneTimeoutMs)
    }

    @Test
    fun `safe mode offset rejects a truncated blob`() {
        assertNull(NativeProfileDocument.safeModeOffset(ByteArray(8)))
    }

    @Test
    fun `unknown route keys are ignored on decode`() {
        val bytes = doc(
            "tcp_zerocopy",
            mapOf(
                "execution.routes.tcp_zerocopy.attempts" to 2000L,
                "execution.routes.tcp_zerocopy.arm_sequence" to 7L,
            ),
        ).toBinary()
        val marker = "tcp_attempts".toByteArray(Charsets.UTF_8)
        val at = bytes.indexOfSubsequence(marker)
        assertTrue(at > 0)
        bytes[at] = 'x'.code.toByte() // same length, unknown key

        val decoded = NativeProfileDocument.fromBinary(bytes)!!
        val config = decoded.routeConfig as TcpConfig
        assertEquals(0u, config.attempts)
        assertEquals(7u, config.armSequence)
    }

    @Test
    fun `decoder rejects a wrong version or magic`() {
        val bytes = doc("select_stack").toBinary()
        val badVersion = bytes.copyOf().also { it[4] = 3 }
        assertNull(NativeProfileDocument.fromBinary(badVersion))
        val badMagic = bytes.copyOf().also { it[0] = 'X'.code.toByte() }
        assertNull(NativeProfileDocument.fromBinary(badMagic))
    }

    @Test
    fun `an oversized release is rejected before encoding`() {
        val document = NativeProfileDocument.from("x".repeat(0x10000), "select_stack", null) { null }
        try {
            document.toBinary()
            fail("expected an IllegalArgumentException")
        } catch (_: IllegalArgumentException) {
        }
    }

    private fun ByteArray.indexOfSubsequence(needle: ByteArray): Int {
        outer@ for (i in 0..size - needle.size) {
            for (j in needle.indices) {
                if (this[i + j] != needle[j]) continue@outer
            }
            return i
        }
        return -1
    }
}
