package com.ghostlock.app.core.victim

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * 能力集与身份读取器单测（批 A，task-52）。
 *
 * 覆盖：能力位唯一/掩码往返/未知位 fail-closed/启用顺序；身份 starttime 解析与负例。
 */
class VictimCapabilityTest {
    @Test
    fun `capability bits are unique and mask roundtrips`() {
        val bits = VictimCapability.entries.map { it.bit }
        assertEquals("能力位必须唯一", bits.size, bits.toSet().size)
        bits.forEach { bit -> assertEquals("能力位必须是单一位", 1, Integer.bitCount(bit)) }
        val all = VictimCapabilities.of(*VictimCapability.entries.toTypedArray())
        assertEquals(
            VictimCapability.entries.size,
            all.enabled().size,
        )
        assertEquals(all.mask, VictimCapabilities.fromMask(all.mask).mask)
        VictimCapability.entries.forEach { capability ->
            assertTrue(all.contains(capability))
            assertFalse(VictimCapabilities.NONE.contains(capability))
        }
    }

    @Test
    fun `unknown capability bit fails closed`() {
        val known = VictimCapability.entries.fold(0) { acc, item -> acc or item.bit }
        try {
            VictimCapabilities.fromMask(known.inv())
            throw AssertionError("unknown mask bit must be rejected")
        } catch (expected: IllegalArgumentException) {
            assertEquals("victim-capability:unknown-bit", expected.message)
        }
    }

    @Test
    fun `identity reader parses stat starttime`() {
        val reader = VictimIdentityReader { pid ->
            val fields = (1..25).joinToString(" ") { index ->
                if (index == 20) "4242" else if (index == 1) "S" else index.toString()
            }
            "$pid (comm) $fields"
        }
        val identity = reader.read(pid = 999, uid = 0L)
        assertEquals(999, identity.pid)
        assertEquals(0L, identity.uid)
        assertEquals(4_242L, identity.startTimeTicks)
    }

    @Test
    fun `identity reader fails closed on malformed input`() {
        fun expectFailure(pid: Int, uid: Long, reader: (Int) -> String?) {
            try {
                VictimIdentityReader(reader).read(pid, uid)
                throw AssertionError("malformed identity must be rejected")
            } catch (expected: VictimIdentityException) {
                assertTrue(expected.reason.startsWith("victim-identity:"))
            }
        }
        expectFailure(0, 0L) { "1 (x) S 1" }
        expectFailure(1, -1L) { "1 (x) S 1" }
        expectFailure(1, 0L) { null }
        expectFailure(1, 0L) { "no-paren-here" }
        expectFailure(1, 0L) { "1 (x) S 1 2 3" }
        expectFailure(1, 0L) {
            val fields = (1..25).joinToString(" ") { index ->
                if (index == 20) "not-a-number" else "1"
            }
            "1 (x) $fields"
        }
    }
}
