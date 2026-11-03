package com.ghostlock.app.core.victim

import com.ghostlock.app.jni.Native43499Jni
import com.ghostlock.app.jni.VictimSpawnJniResult
import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * 实现批 B1（task-53）的单步 victim transport 契约测试。
 *
 * 覆盖：
 * - Kotlin 域校验/错误码映射（纯函数，不触 JNI）；
 * - Kotlin ↔ C 机械错误码对拍（`victim_protocol.h`）；
 * - JNI 9 入口结构契约（符号/转发到 `glk_victim_*`/三态哨兵/无 fork/system）；
 * - Native close/detach 的“释放父侧 fd + free handle”机械事实（源码级）。
 *
 * @note JNI 真实 spawn/wait 行为属设备/E4 门禁；本批无真机执行（UNVERIFIED）。
 */
class VictimNativeJniTest {

    @Test
    fun `spawn validation rejects out-of-domain requests`() {
        assertNull(VictimNativeJni.validateSpawn(validSpawn()))
        assertEquals(
            VictimError.INVALID_ARGUMENT.token,
            VictimNativeJni.validateSpawn(validSpawn().copy(helperPath = "")),
        )
        assertEquals(
            VictimError.INVALID_ARGUMENT.token,
            VictimNativeJni.validateSpawn(validSpawn().copy(argv = emptyList())),
        )
        assertEquals(
            VictimError.INVALID_ARGUMENT.token,
            VictimNativeJni.validateSpawn(
                validSpawn().copy(argv = List(VictimProtocol.MAX_ARG_COUNT + 1) { "x" }),
            ),
        )
        assertEquals(
            VictimError.INVALID_ARGUMENT.token,
            VictimNativeJni.validateSpawn(
                validSpawn().copy(argv = listOf("x".repeat(VictimProtocol.MAX_ARG_BYTES + 1))),
            ),
        )
        assertEquals(
            VictimError.INVALID_ARGUMENT.token,
            VictimNativeJni.validateSpawn(
                validSpawn().copy(env = List(VictimProtocol.MAX_ARG_COUNT + 1) { "K=V" }),
            ),
        )
        assertEquals(
            VictimError.INVALID_ARGUMENT.token,
            VictimNativeJni.validateSpawn(validSpawn().copy(rawSizeBytes = 15L)),
        )
        assertNull(VictimNativeJni.validateSpawn(validSpawn().copy(rawSizeBytes = 16L)))
    }

    @Test
    fun `error codes map to stable victim reasons`() {
        assertEquals(
            VictimError.INVALID_ARGUMENT.token,
            VictimNativeJni.mapError(VictimTransportCodes.INVALID_ARGUMENT, VictimError.SEND_FAILED),
        )
        assertEquals(
            VictimError.BAD_FRAME.token,
            VictimNativeJni.mapError(VictimTransportCodes.BAD_FRAME, VictimError.SEND_FAILED),
        )
        assertEquals(
            VictimError.TIMEOUT.token,
            VictimNativeJni.mapError(VictimTransportCodes.TIMEOUT, VictimError.SEND_FAILED),
        )
        assertEquals(
            VictimError.EOF.token,
            VictimNativeJni.mapError(VictimTransportCodes.EOF, VictimError.SEND_FAILED),
        )
        assertEquals(
            VictimError.SEND_FAILED.token,
            VictimNativeJni.mapError(VictimTransportCodes.SYSTEM, VictimError.SEND_FAILED),
        )
        assertEquals(
            VictimError.WAIT_FAILED.token,
            VictimNativeJni.mapError(VictimTransportCodes.SYSTEM, VictimError.WAIT_FAILED),
        )
        assertEquals(
            VictimError.INVALID_ARGUMENT.token,
            VictimNativeJni.mapError(VictimTransportCodes.JNI_NOT_ACCEPTED_INT, VictimError.SEND_FAILED),
        )
        assertEquals(
            VictimError.SEND_FAILED.token + ":incomplete",
            VictimNativeJni.mapError(VictimTransportCodes.JNI_INCOMPLETE_INT, VictimError.SEND_FAILED),
        )
    }

    @Test
    fun `spawn failure distinguishes invalid argument from spawn error`() {
        val invalid = VictimSpawnJniResult().apply { callStatus = 1 }
        assertEquals(VictimError.INVALID_ARGUMENT.token, VictimNativeJni.spawnFailureReason(invalid))
        val system = VictimSpawnJniResult().apply { callStatus = 8; systemError = 13 }
        assertEquals(VictimError.SPAWN_FAILED.token, VictimNativeJni.spawnFailureReason(system))
    }

    @Test
    fun `native error codes mirror victim protocol header`() {
        val header = readRepoFile("app/src/main/cpp/backends/43499/core/victim_protocol.h")
        val expected = mapOf(
            "GLK_VICTIM_ERR_INVALID_ARGUMENT" to VictimTransportCodes.INVALID_ARGUMENT,
            "GLK_VICTIM_ERR_BAD_FRAME" to VictimTransportCodes.BAD_FRAME,
            "GLK_VICTIM_ERR_TIMEOUT" to VictimTransportCodes.TIMEOUT,
            "GLK_VICTIM_ERR_EOF" to VictimTransportCodes.EOF,
            "GLK_VICTIM_ERR_SYSTEM" to VictimTransportCodes.SYSTEM,
        )
        expected.forEach { (macro, value) ->
            val match = Regex(
                "#define\\s+$macro\\s+INT32_C\\((-?\\d+)\\)",
            ).find(header)
            assertNotNull("C 头缺少 $macro", match)
            assertEquals("C/Kotlin 错误码漂移：$macro", value, match!!.groupValues[1].toInt())
        }
    }

    @Test
    fun `jni entry points forward to mechanical core api only`() {
        val source = readRepoFile("app/src/main/cpp/backends/43499/jni43499.c")
        val entries = listOf(
            "spawnVictim", "sendVictim", "receiveVictim", "readRawVictim",
            "rawSizeVictim", "waitExitVictim", "killVictim", "detachVictim",
            "closeVictim",
        )
        entries.forEach { name ->
            assertTrue(
                "jni43499.c 缺少条目：$name",
                source.contains("Java_com_ghostlock_app_jni_VictimTransportJni_$name"),
            )
        }
        val coreCalls = listOf(
            "glk_victim_spawn(", "glk_victim_send(", "glk_victim_receive(",
            "glk_victim_read_raw(", "glk_victim_raw_size(", "glk_victim_wait(",
            "glk_victim_kill(", "glk_victim_detach(", "glk_victim_close(",
        )
        coreCalls.forEach { call ->
            assertTrue("JNI 未转发到 core：$call", source.contains(call))
        }
        assertTrue(
            "JNI spawn 回执类名必须与 Kotlin DTO 一致（@Keep 固定）",
            source.contains("GLK43499_JNI_CLASS_VICTIM_SPAWN_RESULT") &&
                source.contains("\"com/ghostlock/app/jni/VictimSpawnJniResult\""),
        )
        assertTrue(
            "JNI close 回执类名必须与 Kotlin DTO 一致（@Keep 固定）",
            source.contains("GLK43499_JNI_CLASS_VICTIM_CLOSE_RESULT") &&
                source.contains("\"com/ghostlock/app/jni/VictimCloseJniResult\""),
        )
        assertTrue(source.contains("GLK43499_JNI_NOT_ACCEPTED_HANDLE"))
        assertTrue(source.contains("GLK43499_JNI_INCOMPLETE_HANDLE"))
        assertTrue(source.contains("GLK43499_JNI_NOT_ACCEPTED_INT"))
        assertTrue(source.contains("GLK43499_JNI_INCOMPLETE_INT"))
        assertTrue(
            "JNI 不得解析帧 opcode（业务语义在 Kotlin）",
            !source.contains("GLK_VICTIM_OPCODE_"),
        )
        /* 唯一允许的 fork 调用是 debug-only forkGuardCanary（E4 ①“能失败”验证）。 */
        val forkCalls = Regex("(^|[^A-Za-z0-9_])fork\\s*\\(").findAll(source).count()
        assertEquals("JNI 仅允许 canary 一处 fork()（实得 $forkCalls）", 1, forkCalls)
        val canaryIndex = source.indexOf("forkGuardCanary")
        assertTrue("canary 不存在", canaryIndex >= 0)
        assertTrue("canary 必须子进程立即 _exit", source.contains("if (child == 0) _exit(0);"))
        assertTrue("JNI 不得 vfork", !containsCall(source, "vfork"))
        assertTrue("JNI 不得 system", !containsCall(source, "system"))
        assertTrue("JNI 不得 execve", !containsCall(source, "execve"))
    }

    @Test
    fun `native detach and close release parent fds and free handle`() {
        val source = readRepoFile("app/src/main/cpp/backends/43499/core/victim_protocol.c")
        val release = source.indexOf("static void glk_victim_release_handle")
        assertTrue("缺少 release_handle", release >= 0)
        val body = source.substring(release, (release + 700).coerceAtMost(source.length))
        assertTrue("release 必须 close control fd", body.contains("close(handle->control_fd)"))
        assertTrue("release 必须 close raw fd", body.contains("close(handle->raw_fd)"))
        assertTrue("release 必须 free handle", body.contains("free(handle)"))
        assertTrue("detach 调用 release", source.contains("glk_victim_release_handle(handle)"))
        val closeStart = source.indexOf("int32_t glk_victim_close(")
        assertTrue("缺少 glk_victim_close 定义", closeStart >= 0)
        val closeBody = source.substring(
            closeStart,
            (closeStart + 512).coerceAtMost(source.length),
        )
        assertTrue(
            "close 必须先 kill/reap 再 release",
            closeBody.indexOf("glk_victim_kill(token") in
                0 until closeBody.indexOf("glk_victim_release_handle(handle)"),
        )
    }

    @Test
    fun `kotlin dto and transport object are keep fixed`() {
        val source = readRepoFile("app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt")
        listOf(
            "internal object VictimTransportJni",
            "internal class VictimSpawnJniResult",
            "internal class VictimCloseJniResult",
            "external fun spawnVictim(",
            "external fun sendVictim(",
            "external fun receiveVictim(",
            "external fun readRawVictim(",
            "external fun rawSizeVictim(",
            "external fun waitExitVictim(",
            "external fun killVictim(",
            "external fun detachVictim(",
            "external fun closeVictim(",
        ).forEach { token ->
            assertTrue("Native43499Jni.kt 缺少：$token", source.contains(token))
        }
        /* tri-state 常量与 JNI 同值（Kotlin 侧 owner 结算依据）。 */
        assertEquals(-2, Native43499Jni.RESULT_NOT_ACCEPTED_INT)
        assertEquals(Int.MIN_VALUE, Native43499Jni.RESULT_INCOMPLETE_INT)
        assertEquals(-2L, Native43499Jni.RESULT_NOT_ACCEPTED_HANDLE)
        assertEquals(-1L, Native43499Jni.RESULT_INCOMPLETE_HANDLE)
        assertEquals(Native43499Jni.RESULT_NOT_ACCEPTED_INT, VictimTransportCodes.JNI_NOT_ACCEPTED_INT)
        assertEquals(Native43499Jni.RESULT_INCOMPLETE_INT, VictimTransportCodes.JNI_INCOMPLETE_INT)
        assertEquals(Native43499Jni.RESULT_NOT_ACCEPTED_HANDLE, VictimTransportCodes.JNI_NOT_ACCEPTED_HANDLE)
        assertEquals(Native43499Jni.RESULT_INCOMPLETE_HANDLE, VictimTransportCodes.JNI_INCOMPLETE_HANDLE)
    }

    @Test
    fun `kotlin fork guard debug entries exist`() {
        val source = readRepoFile("app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt")
        listOf(
            "external fun forkGuardSnapshot()",
            "external fun forkGuardReset()",
            "external fun forkGuardCanary()",
        ).forEach { token ->
            assertTrue("Native43499Jni.kt 缺 $token", source.contains(token))
        }
        val jni = readRepoFile("app/src/main/cpp/backends/43499/jni43499.c")
        listOf(
            "Java_com_ghostlock_app_jni_VictimTransportJni_forkGuardSnapshot",
            "Java_com_ghostlock_app_jni_VictimTransportJni_forkGuardReset",
            "Java_com_ghostlock_app_jni_VictimTransportJni_forkGuardCanary",
        ).forEach { token ->
            assertTrue("jni43499.c 缺 $token", jni.contains(token))
        }
    }

    @Test
    fun `kotlin owner close and detach are single call entries`() {
        val source = readRepoFile(
            "app/src/main/kotlin/com/ghostlock/app/core/victim/VictimNativeJni.kt",
        )
        /* owner 语义：close 只有一个 JNI 调用点（VictimSession token=0 保证恰好一次）；
         * detach 两个调用点 = owner detach + spawn 的 pid 异常清理，均无重试/循环。 */
        assertEquals(1, Regex("VictimTransportJni\\.closeVictim\\(").findAll(source).count())
        assertEquals(2, Regex("VictimTransportJni\\.detachVictim\\(").findAll(source).count())
        assertTrue("close 不得重试/循环", !source.contains("while (") || !source.contains("closeVictim"))
    }

    private fun validSpawn(): VictimSpawnRequest = VictimSpawnRequest(
        helperPath = "/data/app/lib/libghostlock_victim_helper.so",
        argv = listOf("ghostlock_victim_helper"),
        env = emptyList(),
        rawSizeBytes = 4096L,
    )

    private fun containsCall(text: String, name: String): Boolean =
        Regex("(^|[^A-Za-z0-9_])" + Regex.escape(name) + "\\s*\\(").containsMatchIn(text)

    private fun readRepoFile(relative: String): String {
        val candidates = listOf(File(relative), File("../$relative"), File("../../$relative"))
        return candidates.firstOrNull { it.isFile }?.readText()
            ?: error("not found: $relative")
    }
}
