package com.ghostlock.app.core.victim

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * G04/task-80 Kotlin 侧 PROBE append 单测（先写不运行）。
 *
 * 覆盖：opcode 6/7/8 线帧解码、capability bits 7/8/9、marker 校验、
 * PROBE_STATUS 4 KiB 上限、PROBE_COMM len0/8/15 解读。
 */
class VictimProbeAppendTest {

    @Test
    fun `append opcodes decode with frozen wire values`() {
        for ((opcode, frame) in listOf(
            VictimProtocol.Opcode.PROBE_STATUS to VictimProtocol.encodeProbeStatus(),
            VictimProtocol.Opcode.PROBE_COMM to VictimProtocol.encodeProbeComm(),
        )) {
            val decoded = VictimProtocol.decodeCommand(frame)
            assertTrue(decoded is VictimResult.Ok)
            assertEquals(opcode, (decoded as VictimResult.Ok).value.opcode)
        }
    }

    @Test
    fun `probe comm set encodes marker and rejects unsafe markers`() {
        val frame = VictimProtocol.encodeProbeCommSet("GLK_LEAF_1".toByteArray())
        val decoded = VictimProtocol.decodeCommand(frame) as VictimResult.Ok
        assertEquals(VictimProtocol.Opcode.PROBE_COMM_SET, decoded.value.opcode)
        assertArrayEquals("GLK_LEAF_1".toByteArray(), decoded.value.args)
        assertThrows(IllegalArgumentException::class.java) {
            VictimProtocol.encodeProbeCommSet(ByteArray(0))
        }
        assertThrows(IllegalArgumentException::class.java) {
            VictimProtocol.encodeProbeCommSet(ByteArray(16) { 'a'.code.toByte() })
        }
        assertThrows(IllegalArgumentException::class.java) {
            VictimProtocol.encodeProbeCommSet("has space".toByteArray())
        }
    }

    @Test
    fun `capability bits 7 8 9 are appended`() {
        val all = VictimCapabilities.of(*VictimCapability.entries.toTypedArray())
        assertTrue(all.contains(VictimCapability.PROBE_STATUS))
        assertTrue(all.contains(VictimCapability.PROBE_COMM_SET))
        assertTrue(all.contains(VictimCapability.PROBE_COMM))
        assertEquals(1 shl 7, VictimCapability.PROBE_STATUS.bit)
        assertEquals(1 shl 8, VictimCapability.PROBE_COMM_SET.bit)
        assertEquals(1 shl 9, VictimCapability.PROBE_COMM.bit)
    }

    @Test
    fun `status reply is bounded at 4 kib`() {
        val ok = VictimProtocol.decodeProbeStatusReply(ByteArray(4096) { 'x'.code.toByte() })
        assertTrue(ok is VictimResult.Ok)
        assertTrue(VictimProtocol.decodeProbeStatusReply(ByteArray(4097)) is VictimResult.Failed)
        assertTrue(VictimProtocol.decodeProbeStatusReply(ByteArray(0)) is VictimResult.Failed)
    }

    @Test
    fun `comm reply decodes len 0 8 15`() {
        for (length in listOf(0, 8, 15)) {
            val data = ByteArray(2 + length).also {
                it[0] = length.toByte()
                it[1] = if (length == 0) 0 else 'A'.code.toByte()
                for (index in 0 until length) it[2 + index] = 'a'.code.toByte()
            }
            val decoded = VictimProtocol.decodeProbeCommReply(data) as VictimResult.Ok
            assertEquals(length, decoded.value.length)
            assertEquals(length, decoded.value.bytes.size)
        }
        assertTrue(VictimProtocol.decodeProbeCommReply(byteArrayOf(3, 65, 1, 2)) is VictimResult.Failed)
        assertTrue(VictimProtocol.decodeProbeCommReply(ByteArray(0)) is VictimResult.Failed)
    }
}
