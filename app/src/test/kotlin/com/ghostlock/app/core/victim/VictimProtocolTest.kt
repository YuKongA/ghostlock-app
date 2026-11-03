package com.ghostlock.app.core.victim

import java.io.File
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test

/**
 * 通用 victim 控制协议 codec 单测（批 A，task-52）。
 *
 * 覆盖：命令/回复帧往返、长度上限与越界、CRC/版本/类型校验、raw 帧往返与截断、
 * 输出 SHA-256；并用 C 头文件逐常量对拍防跨语言漂移。
 */
class VictimProtocolTest {
    @Test
    fun `protocol constants mirror native header`() {
        val header = readHeader()
        val expected = linkedMapOf(
            "GLK_VICTIM_PROTOCOL_VERSION" to VictimProtocol.VERSION,
            "GLK_VICTIM_CONTROL_FD" to VictimProtocol.CONTROL_FD,
            "GLK_VICTIM_RAW_FD" to VictimProtocol.RAW_FD,
            "GLK_VICTIM_FRAME_LENGTH_PREFIX_BYTES" to VictimProtocol.LENGTH_PREFIX_BYTES,
            "GLK_VICTIM_BODY_HEADER_BYTES" to VictimProtocol.BODY_HEADER_BYTES,
            "GLK_VICTIM_CHECKSUM_BYTES" to VictimProtocol.CHECKSUM_BYTES,
            "GLK_VICTIM_RAW_HEADER_BYTES" to VictimProtocol.RAW_HEADER_BYTES,
            "GLK_VICTIM_RAW_MAGIC" to VictimProtocol.RAW_MAGIC,
            "GLK_VICTIM_RAW_VERSION" to VictimProtocol.RAW_VERSION,
            "GLK_VICTIM_RAW_FLAG_TRUNCATED" to VictimProtocol.RAW_FLAG_TRUNCATED,
            "GLK_VICTIM_FRAME_KIND_COMMAND" to VictimProtocol.FRAME_KIND_COMMAND,
            "GLK_VICTIM_FRAME_KIND_REPLY" to VictimProtocol.FRAME_KIND_REPLY,
            "GLK_VICTIM_MAX_SCRIPT_PATH_BYTES" to VictimProtocol.MAX_SCRIPT_PATH_BYTES,
            "GLK_VICTIM_MAX_ARG_BYTES" to VictimProtocol.MAX_ARG_BYTES,
            "GLK_VICTIM_MAX_ARG_COUNT" to VictimProtocol.MAX_ARG_COUNT,
        )
        expected.forEach { (name, value) ->
            val matched = Regex(
                "#define\\s+" + Regex.escape(name) +
                    "\\s+(?:U?INT(?:32|16)?_C\\()?([0-9xA-Fa-f]+)",
            ).find(header)
            assertTrue("C 头缺少常量 $name", matched != null)
            val text = matched!!.groupValues[1]
            val parsed = if (text.startsWith("0x", ignoreCase = true)) {
                text.substring(2).toLong(16).toInt()
            } else {
                text.toInt()
            }
            assertEquals("常量漂移：$name", value, parsed)
        }
        assertTrue(
            "GLK_VICTIM_FRAME_MAX_BODY 必须存在且为 1<<12 表达",
            Regex(
                "GLK_VICTIM_FRAME_MAX_BODY\\s+\\(?UINT32_C\\(1\\)\\s*<<\\s*12\\)?",
            ).containsMatchIn(header),
        )
        assertEquals(4096, VictimProtocol.FRAME_MAX_BODY)
        val opcodes = mapOf(
            "GLK_VICTIM_OPCODE_PROBE_UID" to VictimProtocol.Opcode.PROBE_UID.wire,
            "GLK_VICTIM_OPCODE_PERF_SELF_SAMPLE" to VictimProtocol.Opcode.PERF_SELF_SAMPLE.wire,
            "GLK_VICTIM_OPCODE_EXEC_SCRIPT" to VictimProtocol.Opcode.EXEC_SCRIPT.wire,
            "GLK_VICTIM_OPCODE_PARK" to VictimProtocol.Opcode.PARK.wire,
            "GLK_VICTIM_OPCODE_BYE" to VictimProtocol.Opcode.BYE.wire,
        )
        opcodes.forEach { (name, value) ->
            assertTrue(
                "C 头缺少 opcode $name",
                Regex("#define\\s+" + Regex.escape(name) + "\\s+" + value).containsMatchIn(header) ||
                    Regex("$name\\s*=\\s*$value").containsMatchIn(header),
            )
        }
        val statuses = mapOf(
            "GLK_VICTIM_STATUS_OK" to VictimProtocol.Status.OK.wire,
            "GLK_VICTIM_STATUS_ERROR" to VictimProtocol.Status.ERROR.wire,
        )
        statuses.forEach { (name, value) ->
            assertTrue(
                "C 头缺少 status $name",
                Regex("$name\\s*=\\s*$value").containsMatchIn(header),
            )
        }
    }

    @Test
    fun `probe park bye command roundtrip`() {
        val probe = VictimProtocol.decodeCommand(VictimProtocol.encodeProbeUid())
        assertEquals(
            VictimProtocol.Opcode.PROBE_UID,
            (probe as VictimResult.Ok).value.opcode,
        )
        assertEquals(0, (probe.value as VictimProtocol.Command).args.size)
        val park = VictimProtocol.decodeCommand(VictimProtocol.encodePark())
        assertEquals(VictimProtocol.Opcode.PARK, (park as VictimResult.Ok).value.opcode)
        val bye = VictimProtocol.decodeCommand(VictimProtocol.encodeBye())
        assertEquals(VictimProtocol.Opcode.BYE, (bye as VictimResult.Ok).value.opcode)
    }

    @Test
    fun `perf sample command roundtrip`() {
        val request = VictimProtocol.PerfSampleRequest(
            attrType = 1L,
            attrConfig = 0L,
            attrSamplePeriod = 1_000L,
            attrSampleType = 0x40001L,
            attrSampleRegsIntr = 0xFFL,
            attrFlags = 8L,
            ringPages = 2L,
            iterations = 32L,
            rawOffset = 0L,
            rawCapacity = 1024L,
        )
        val wire = VictimProtocol.encodePerfSelfSample(request)
        val command = (VictimProtocol.decodeCommand(wire) as VictimResult.Ok).value
        assertEquals(VictimProtocol.Opcode.PERF_SELF_SAMPLE, command.opcode)
        val decoded = (VictimProtocol.decodePerfSelfSampleArgs(command.args) as VictimResult.Ok).value
        assertEquals(request, decoded)
        assertTrue(wire.size <= VictimProtocol.WIRE_MAX_BYTES)
    }

    @Test
    fun `exec script command roundtrip with argv env`() {
        val request = VictimProtocol.ExecScriptRequest(
            scriptPath = "/data/user/0/app/files/finish/run.sh",
            argv = listOf("--flag", "value"),
            env = listOf("STATUS=0"),
            timeoutMs = 30_000L,
        )
        val wire = VictimProtocol.encodeExecScript(request)
        val command = (VictimProtocol.decodeCommand(wire) as VictimResult.Ok).value
        assertEquals(VictimProtocol.Opcode.EXEC_SCRIPT, command.opcode)
        val decoded = (VictimProtocol.decodeExecScriptArgs(command.args) as VictimResult.Ok).value
        assertEquals(request, decoded)
    }

    @Test
    fun `reply roundtrip ok and error`() {
        val okWire = VictimProtocol.encodeReply(
            status = VictimProtocol.Status.OK,
            data = byteArrayOf(1, 2, 3),
        )
        val ok = (VictimProtocol.decodeReply(okWire) as VictimResult.Ok).value
        assertEquals(VictimProtocol.Status.OK, ok.status)
        assertNull(ok.errorCode)
        assertArrayEquals(byteArrayOf(1, 2, 3), ok.data)

        val errorWire = VictimProtocol.encodeReply(
            status = VictimProtocol.Status.ERROR,
            errorCode = VictimProtocol.ErrorCode.TIMEOUT,
            errorErrno = 110,
        )
        val error = (VictimProtocol.decodeReply(errorWire) as VictimResult.Ok).value
        assertEquals(VictimProtocol.Status.ERROR, error.status)
        assertEquals(VictimProtocol.ErrorCode.TIMEOUT, error.errorCode)
        assertEquals(110, error.errorErrno)
    }

    @Test
    fun `decoder rejects version kind crc and length corruption`() {
        val frame = VictimProtocol.encodeProbeUid()
        /* 版本字节：wire=4 前缀 + body[0]=version。 */
        val badVersion = frame.copyOf()
        badVersion[4] = 9
        assertFailed(VictimProtocol.decodeCommand(badVersion))
        /* kind 字节。 */
        val badKind = frame.copyOf()
        badKind[5] = 9
        assertFailed(VictimProtocol.decodeCommand(badKind))
        /* 校验和最后一个字节。 */
        val badCrc = frame.copyOf()
        badCrc[badCrc.size - 1] = (badCrc[badCrc.size - 1].toInt() xor 0xFF).toByte()
        assertFailed(VictimProtocol.decodeCommand(badCrc))
        /* 外层长度与 body 不一致。 */
        val badLength = frame.copyOf()
        badLength[0] = (badLength[0].toInt() + 1).toByte()
        assertFailed(VictimProtocol.decodeCommand(badLength))
        /* body 上限：伪造 bodyLen = MAX+1。 */
        val oversize = ByteArray(8)
        oversize[0] = ((VictimProtocol.FRAME_MAX_BODY + 1) and 0xFF).toByte()
        oversize[1] = (((VictimProtocol.FRAME_MAX_BODY + 1) shr 8) and 0xFF).toByte()
        assertFailed(VictimProtocol.decodeCommand(oversize))
    }

    @Test
    fun `encoder rejects oversized payload and invalid args`() {
        /* 大量 argv/env 使 payload 超限 ⇒ 编码拒绝（不截断）。 */
        val tooBig = VictimProtocol.ExecScriptRequest(
            scriptPath = "/a",
            argv = List(VictimProtocol.MAX_ARG_COUNT) { "x".repeat(VictimProtocol.MAX_ARG_BYTES) },
            env = List(VictimProtocol.MAX_ARG_COUNT) { "y".repeat(VictimProtocol.MAX_ARG_BYTES) },
            timeoutMs = 1_000L,
        )
        try {
            VictimProtocol.encodeExecScript(tooBig)
            fail("oversized payload must be rejected")
        } catch (expected: IllegalArgumentException) {
            assertTrue(expected.message!!.contains("payload-size"))
        }
        try {
            VictimProtocol.encodeExecScript(
                VictimProtocol.ExecScriptRequest("/a", List(17) { "x" }, emptyList(), 1_000L),
            )
            fail("too many argv must be rejected")
        } catch (expected: IllegalArgumentException) {
            assertTrue(expected.message!!.contains("argv-count"))
        }
        try {
            VictimProtocol.encodePerfSelfSample(
                VictimProtocol.PerfSampleRequest(1, 0, 1, 1, 1, 0, 0, 1, 0, 128),
            )
            fail("zero ring pages must be rejected")
        } catch (expected: IllegalArgumentException) {
            assertTrue(expected.message!!.contains("ring-pages"))
        }
    }

    @Test
    fun `raw frame roundtrip and validation`() {
        val payload = byteArrayOf(9, 8, 7)
        val encoded = VictimProtocol.encodeRawFrame(payload, truncated = true)
        val decoded = (VictimProtocol.decodeRawFrame(encoded) as VictimResult.Ok).value
        assertArrayEquals(payload, decoded.data)
        assertTrue(decoded.truncated)
        assertEquals(
            16 + payload.size,
            encoded.size,
        )
        /* 期望长度不符。 */
        assertFailed(VictimProtocol.decodeRawFrame(encoded, 2))
        /* 魔法破坏。 */
        val badMagic = encoded.copyOf()
        badMagic[0] = 0
        assertFailed(VictimProtocol.decodeRawFrame(badMagic))
        /* 数据区不足。 */
        assertFailed(
            VictimProtocol.decodeRawFrame(
                encoded.copyOfRange(0, encoded.size - 1),
            ),
        )
        /* 头都不足。 */
        assertFailed(VictimProtocol.decodeRawFrame(ByteArray(4)))
    }

    @Test
    fun `sha256 matches known vector`() {
        assertEquals(
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
            VictimProtocol.sha256Hex("abc".toByteArray()),
        )
        assertEquals(
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
            VictimProtocol.sha256Hex(ByteArray(0)),
        )
    }

    /**
     * @param result 待检查结果。
     * @note 断言为 Failed 且 reason 非空。
     */
    private fun assertFailed(result: VictimResult<*>) {
        assertTrue("expected failed but was $result", result is VictimResult.Failed)
        assertTrue((result as VictimResult.Failed).reason.isNotBlank())
    }

    /** @return C 头文本（repo 根解析，含 IDE/命令行两种 cwd）。 */
    private fun readHeader(): String {
        val candidates = listOf(
            File("app/src/main/cpp/backends/43499/core/victim_protocol.h"),
            File("../app/src/main/cpp/backends/43499/core/victim_protocol.h"),
            File("../../app/src/main/cpp/backends/43499/core/victim_protocol.h"),
        )
        val file = candidates.firstOrNull { it.isFile }
            ?: error("victim_protocol.h not found from ${File(".").absolutePath}")
        return file.readText()
    }

    @Test
    fun `fork guard bye reply decodes two counters`() {
        val zero = when (val decoded = VictimProtocol.decodeForkGuardReply(ByteArray(8))) {
            is VictimResult.Ok -> decoded.value
            is VictimResult.Failed -> error("decode failed: ${decoded.reason}")
        }
        assertEquals(0L, zero.fork)
        assertEquals(0L, zero.vfork)
        val bytes = ByteArray(8)
        bytes[0] = 1
        bytes[4] = 2
        val one = when (val decoded = VictimProtocol.decodeForkGuardReply(bytes)) {
            is VictimResult.Ok -> decoded.value
            is VictimResult.Failed -> error("decode failed: ${decoded.reason}")
        }
        assertEquals(1L, one.fork)
        assertEquals(2L, one.vfork)
        assertTrue(VictimProtocol.decodeForkGuardReply(ByteArray(4)) is VictimResult.Failed)
        assertTrue(VictimProtocol.decodeForkGuardReply(ByteArray(12)) is VictimResult.Failed)
    }

}
