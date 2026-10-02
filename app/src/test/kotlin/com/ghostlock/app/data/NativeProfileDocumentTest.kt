package com.ghostlock.app.data

import com.ghostlock.app.data.route.MulticastConfig
import com.ghostlock.app.data.route.SelectConfig
import com.ghostlock.app.data.route.TcpConfig
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test

/**
 * White-box tests for the v2 object-section transport shared with native
 * (`profile/binary.cpp`): exact header bytes, section/entry framing, the
 * per-route short keys, presence semantics and malformed-document rejection.
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

    private fun releaseLength(bytes: ByteArray): Int = readU16(bytes, 12)

    private data class RawSection(val name: String, val entries: List<Pair<String, Long>>)

    private fun sections(bytes: ByteArray): List<RawSection> {
        var p = 16 + releaseLength(bytes)
        val count = readU16(bytes, p)
        p += 2
        val out = mutableListOf<RawSection>()
        repeat(count) {
            val nameLength = bytes[p++].toInt() and 0xff
            val name = String(bytes, p, nameLength, Charsets.UTF_8)
            p += nameLength
            val entryCount = readU32(bytes, p).toInt()
            p += 4
            val entries = mutableListOf<Pair<String, Long>>()
            repeat(entryCount) {
                val keyLength = bytes[p++].toInt() and 0xff
                val key = String(bytes, p, keyLength, Charsets.UTF_8)
                p += keyLength
                entries += key to readLong(bytes, p)
                p += 8
            }
            out += RawSection(name, entries)
        }
        return out
    }

    private fun entriesOf(bytes: ByteArray, section: String): List<Pair<String, Long>> =
        sections(bytes).firstOrNull { it.name == section }?.entries ?: emptyList()

    @Test
    fun `header uses the agreed magic and version`() {
        val bytes = doc("select_stack").toBinary()
        assertEquals(0x0D000721L, NativeProfileDocument.Magic.toLong())
        assertEquals(2, NativeProfileDocument.Version.toInt())
        assertEquals(0x0D000721L, readU32(bytes, 0))
        assertEquals(2, readU16(bytes, 4))
        assertEquals(1, readU16(bytes, 6)) // frontend root_child
        assertEquals(1, readU16(bytes, 8)) // backend cve_2026_43499
        assertEquals(2, readU16(bytes, 10)) // select_stack
        assertEquals(release.length, releaseLength(bytes))
        assertEquals(release, String(bytes, 16, releaseLength(bytes), Charsets.UTF_8))
    }

    @Test
    fun `route section carries exactly the route's short keys`() {
        assertEquals(
            listOf("attempts", "arm_sequence", "post_receive_hold_iterations"),
            entriesOf(doc("tcp_zerocopy").toBinary(), "route.tcp_zerocopy").map { it.first },
        )
        assertEquals(
            listOf("waiter_shift", "compact_waiter", "enter_delay_us", "timeout_us"),
            entriesOf(
                doc(
                    "select_stack",
                    mapOf(
                        "pselect_waiter_shift" to -2L,
                        "compact_waiter" to 1L,
                        "execution.routes.select_stack.enter_delay_us" to 50000L,
                        "execution.routes.select_stack.timeout_us" to 1000L,
                    ),
                ).toBinary(),
                "route.select_stack",
            ).map { it.first },
        )
        assertEquals(
            listOf("waiter_off", "buffer_size", "task_offset", "lock_offset"),
            entriesOf(
                doc(
                    "multicast_waiter",
                    mapOf(
                        "mcast.waiter_off" to 264L,
                        "mcast.buffer_size" to 512L,
                        "mcast.task_offset" to 0x40L,
                        "mcast.lock_offset" to 0x50L,
                    ),
                ).toBinary(),
                "route.multicast_waiter",
            ).map { it.first },
        )
        /* An unresolved route emits no route section. */
        val unresolved = doc(route = null).toBinary()
        assertEquals(0, readU16(unresolved, 10))
        assertTrue(sections(unresolved).none { it.name.startsWith("route.") })
    }

    @Test
    fun `optional fields are omitted while presence is carried by keys`() {
        val bytes = doc(
            "multicast_waiter",
            mapOf(
                "mcast.waiter_off" to 264L,
                "mcast.buffer_size" to 0L, // provided 0 must still appear
            ),
        ).toBinary()
        val route = entriesOf(bytes, "route.multicast_waiter")
        assertEquals(listOf("waiter_off", "buffer_size"), route.map { it.first })
        assertEquals(264L, route.toMap()["waiter_off"])
        assertEquals(0L, route.toMap()["buffer_size"])
        /* Nothing in `kernel` was provided, so the section is absent. */
        assertTrue(entriesOf(bytes, "kernel").isEmpty())

        val decoded = NativeProfileDocument.fromBinary(bytes)!!
        val geometry = (decoded.routeConfig as MulticastConfig).geometry
        assertEquals(264, geometry.waiterOff)
        assertEquals(0u, geometry.bufferSize)
        assertNull(decoded.kernelPhysLoad)
        assertNull(decoded.compactWaiter)
    }

    @Test
    fun `multicast tuning keys mirror the native route section`() {
        val bytes = doc(
            "multicast_waiter",
            mapOf(
                "mcast.attempts" to 128L,
                "mcast.arm_sequence" to 16L,
                "mcast.arm_hold" to 20000L,
            ),
        ).toBinary()
        val route = entriesOf(bytes, "route.multicast_waiter")
        assertEquals(listOf("attempts", "arm_sequence", "arm_hold"), route.map { it.first })
        val config = NativeProfileDocument.fromBinary(bytes)!!.routeConfig as MulticastConfig
        assertEquals(128u.toUByte(), config.attempts)
        assertEquals(16u.toUByte(), config.armSequence)
        assertEquals(20000u.toUShort(), config.armHold)
        /* Absent keys keep the compiled route defaults and stay out of the bytes. */
        val bare = doc("multicast_waiter", mapOf("mcast.waiter_off" to 264L)).toBinary()
        assertEquals(
            listOf("waiter_off"),
            entriesOf(bare, "route.multicast_waiter").map { it.first },
        )
    }

    @Test
    fun `signed values keep their two's complement bits`() {
        val bytes = doc("select_stack", mapOf("pselect_waiter_shift" to -2L)).toBinary()
        assertEquals(-2L, entriesOf(bytes, "route.select_stack").toMap()["waiter_shift"]!!)
        val decoded = NativeProfileDocument.fromBinary(bytes)!!
        assertEquals(-2, (decoded.routeConfig as SelectConfig).waiterShift)
    }

    @Test
    fun `unsigned values round trip exactly without clamping`() {
        val bytes = doc(
            "tcp_zerocopy",
            mapOf("execution.routes.tcp_zerocopy.attempts" to 0xFFFFFFFFL),
        ).toBinary()
        val decoded = NativeProfileDocument.fromBinary(bytes)!!
        assertEquals(0xFFFFFFFFu, (decoded.routeConfig as TcpConfig).attempts)
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
        val marker = "attempts".toByteArray(Charsets.UTF_8)
        /* Route sections come last, so the trailing match is the route key. */
        val at = bytes.lastIndexOfSubsequence(marker)
        assertTrue(at > 0)
        bytes[at] = 'x'.code.toByte() // same length, unknown key

        val decoded = NativeProfileDocument.fromBinary(bytes)!!
        val config = decoded.routeConfig as TcpConfig
        assertEquals(0u, config.attempts)
        assertEquals(7u, config.armSequence)
    }

    @Test
    fun `patch safe mode rewrites the meta entry`() {
        val bytes = doc("select_stack", mapOf("kernel_major" to 6L)).toBinary()
        assertEquals(0L, entriesOf(bytes, "meta").toMap()["safe_mode"])
        val patched = NativeProfileDocument.patchSafeMode(bytes)!!
        assertEquals(1L, entriesOf(patched, "meta").toMap()["safe_mode"])
        assertEquals(1u, NativeProfileDocument.fromBinary(patched)!!.safeMode)
        /* Original input is untouched. */
        assertEquals(0L, entriesOf(bytes, "meta").toMap()["safe_mode"])
        assertNull(NativeProfileDocument.patchSafeMode(ByteArray(8)))
    }

    @Test
    fun `decoder rejects a wrong version or magic`() {
        val bytes = doc("select_stack").toBinary()
        val badVersion = bytes.copyOf().also { it[4] = 9 }
        assertNull(NativeProfileDocument.fromBinary(badVersion))
        val badMagic = bytes.copyOf().also { it[0] = 'X'.code.toByte() }
        assertNull(NativeProfileDocument.fromBinary(badMagic))
    }

    @Test
    fun `decoder rejects an unknown route id`() {
        val bytes = doc("select_stack").toBinary()
        val badRoute = bytes.copyOf().also { it[10] = 99 }
        assertNull(NativeProfileDocument.fromBinary(badRoute))
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

    private fun ByteArray.lastIndexOfSubsequence(needle: ByteArray): Int {
        outer@ for (i in size - needle.size downTo 0) {
            for (j in needle.indices) {
                if (this[i + j] != needle[j]) continue@outer
            }
            return i
        }
        return -1
    }
}
