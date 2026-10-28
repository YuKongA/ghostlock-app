package com.ghostlock.app.backend.cve43499.victim

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * victim 行协议纯函数单测（R3f/task-45）：时间换算/路径校验/命令编码/marker 解析/
 * 输出摘要。不触进程。
 */
class VictimProtocolTest {
    @Test
    fun `timeout seconds round up and reject non positive`() {
        assertEquals(1L, VictimProtocol.timeoutSeconds(1L))
        assertEquals(1L, VictimProtocol.timeoutSeconds(1_000L))
        assertEquals(2L, VictimProtocol.timeoutSeconds(1_001L))
        assertEquals(30L, VictimProtocol.timeoutSeconds(30_000L))
        assertNull(VictimProtocol.timeoutSeconds(0L))
        assertNull(VictimProtocol.timeoutSeconds(-5L))
    }

    @Test
    fun `script path validation is fail closed`() {
        assertNull(VictimProtocol.validateScriptPath("/data/user/0/app/files/finish/run.sh"))
        assertEquals("blank", VictimProtocol.validateScriptPath(""))
        assertEquals("blank", VictimProtocol.validateScriptPath("   "))
        assertEquals("not-absolute", VictimProtocol.validateScriptPath("run.sh"))
        assertEquals("control-char", VictimProtocol.validateScriptPath("/tmp/a\nb"))
        assertEquals("control-char", VictimProtocol.validateScriptPath("/tmp/a\rb"))
        assertEquals("control-char", VictimProtocol.validateScriptPath("/tmp/a\u0000b"))
        assertEquals("quote-char", VictimProtocol.validateScriptPath("/tmp/a'b"))
        assertEquals("space-char", VictimProtocol.validateScriptPath("/tmp/a b"))
        assertEquals("space-char", VictimProtocol.validateScriptPath("/tmp/a\tb"))
        assertEquals(
            "too-long",
            VictimProtocol.validateScriptPath(
                "/" + "a".repeat(VictimProtocol.MAX_SCRIPT_PATH_LENGTH),
            ),
        )
    }

    @Test
    fun `finish command encodes timeout and single quoted path`() {
        val command = VictimProtocol.finishCommand(
            timeoutMs = 5_001L,
            scriptPath = "/data/user/0/app/files/finish/run.sh",
        )
        assertEquals("finish 6 '/data/user/0/app/files/finish/run.sh'", command)
        assertNull(VictimProtocol.finishCommand(0L, "/a"))
        assertNull(VictimProtocol.finishCommand(1_000L, "relative"))
        assertNull(VictimProtocol.finishCommand(1_000L, "/a\nb"))
        assertNull(VictimProtocol.finishCommand(1_000L, "/a'b"))
    }

    @Test
    fun `exit and timeout markers parse`() {
        assertEquals(0, VictimProtocol.parseExitMarker("GLK_FINISH_EXIT:0"))
        assertEquals(137, VictimProtocol.parseExitMarker("GLK_FINISH_EXIT:137"))
        assertEquals(-1, VictimProtocol.parseExitMarker("GLK_FINISH_EXIT:-1"))
        assertNull(VictimProtocol.parseExitMarker("GLK_FINISH_EXIT:abc"))
        assertNull(VictimProtocol.parseExitMarker("ordinary output"))
        assertNull(VictimProtocol.parseExitMarker("GLK_FINISH_EXIT:"))
        assertTrue(VictimProtocol.isTimeoutMarker(VictimProtocol.TIMEOUT_MARKER))
        assertFalse(VictimProtocol.isTimeoutMarker("ordinary output"))
    }

    @Test
    fun `output digest hashes lines with trailing newline`() {
        val digest = java.security.MessageDigest.getInstance("SHA-256")
        VictimProtocol.updateDigest(digest, "line-a")
        VictimProtocol.updateDigest(digest, "line-b")
        assertEquals(
            VictimProtocol.sha256Hex("line-a\nline-b\n".toByteArray()),
            hex(digest.digest()),
        )
        assertEquals(
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
            VictimProtocol.sha256Hex("abc".toByteArray()),
        )
    }

    /** @param bytes 原始字节。 @return 小写十六进制。 */
    private fun hex(bytes: ByteArray): String =
        bytes.joinToString("") { byte -> "%02x".format(byte) }

    @Test
    fun `default command contains the whole line protocol`() {
        val command = ProcessVictim.DEFAULT_COMMAND
        assertEquals(3, command.size)
        assertEquals("/system/bin/sh", command[0])
        assertEquals("-c", command[1])
        val script = command[2]
        assertTrue(script.contains("probe"))
        assertTrue(script.contains("id -u"))
        assertTrue(script.contains("finish"))
        assertTrue(script.contains("park"))
        assertTrue(script.contains("bye"))
        assertTrue(script.contains(VictimProtocol.EXIT_MARKER_PREFIX))
        assertTrue(script.contains(VictimProtocol.TIMEOUT_MARKER))
        assertTrue(script.contains("/system/bin/sh"))
    }
}
