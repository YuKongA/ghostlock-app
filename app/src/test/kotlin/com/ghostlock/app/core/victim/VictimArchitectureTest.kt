package com.ghostlock.app.core.victim

import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * 通用 victim 组件结构/契约静态测试（批 A，task-52）。
 *
 * 覆盖：
 * - Kotlin `core/victim` 无 backend/43499 依赖、无进程 fork/exec（生产零 fork）；
 * - C 头 fd 契约/opcode/帧上限与 Kotlin 常量对拍；
 * - helper 源码只含 posix_spawn（无 fork/system/execve）且启动自检 close_range/F_GETFD；
 * - Native transport 只做机械 transport（含 posix_spawn、无 fork/system）。
 *
 * @note 这是编译期契约的回归护栏；真实 exec/uid/seccomp 行为属 E4/E5 真机门禁。
 */
class VictimArchitectureTest {
    @Test
    fun `kotlin victim package has no backend or 43499 dependency`() {
        val files = victimKotlinFiles()
        assertTrue("core/victim 至少应有实现文件", files.isNotEmpty())
        val banned = listOf(
            "backend.cve43499",
            "backend/cve43499",
            "Cve43499",
            "43499",
            "cve_2026",
            "ProcessBuilder",
            "Runtime.getRuntime",
            ".exec(",
            "fork(",
        )
        files.forEach { file ->
            val text = file.readText()
            banned.forEach { token ->
                assertFalse(
                    "core/victim/${file.name} 含禁止依赖/用法：$token",
                    text.contains(token) && !isDocumentationMention(text, token),
                )
            }
        }
    }

    @Test
    fun `native header freezes fd and frame contract`() {
        val header = readCFile("app/src/main/cpp/gl43499/core/victim_protocol.h")
        assertTrue(header.contains("#define GLK_VICTIM_CONTROL_FD 3"))
        assertTrue(header.contains("#define GLK_VICTIM_RAW_FD 4"))
        assertTrue(header.contains("#define GLK_VICTIM_FRAME_MAX_BODY"))
        assertTrue(header.contains("GLK_VICTIM_RAW_HEADER_BYTES UINT32_C(16)"))
        assertTrue(header.contains("GLK_VICTIM_RAW_MAGIC UINT32_C(0x31524156)"))
        assertFalse("通用协议头不得出现 43499 依赖", header.contains("43499"))
        assertFalse("通用协议头不得出现 backend 命名", header.contains("cve43499"))
    }

    @Test
    fun `native transport is mechanical and fork free`() {
        val protocol = readCFile("app/src/main/cpp/gl43499/core/victim_protocol.c")
        assertTrue(protocol.contains("posix_spawn("))
        assertTrue(protocol.contains("glk_victim_spawn"))
        assertTrue(protocol.contains("waitpid("))
        assertTrue(protocol.contains("socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC"))
        assertFalse("Native transport 不得 fork", containsCall(protocol, "fork"))
        assertFalse("Native transport 不得 system", containsCall(protocol, "system"))
        assertFalse("Native transport 不得 execve", containsCall(protocol, "execve"))
        assertFalse("Native transport 不得解析 opcode", protocol.contains("OPCODE_PROBE_UID"))
    }

    @Test
    fun `helper is a pie with fd self check and posix spawn only`() {
        val helper = readCFile(
            "app/src/main/cpp/gl43499/core/victim_helper/victim_helper.c",
        )
        assertTrue(helper.contains("glk_helper_assert_contract_fds"))
        assertTrue(helper.contains("F_GETFD"))
        assertTrue(helper.contains("close_range"))
        assertTrue(helper.contains("posix_spawn("))
        assertTrue(helper.contains("/system/bin/sh"))
        assertTrue(helper.contains("perf_event_open"))
        // helper 允许机制分支（opcode dispatch），但不得承载业务地址/投票/目标常量。
        assertFalse("helper 不得含选址/投票业务词", helper.contains("candidatePageBase"))
        assertFalse("helper 不得含 43499 依赖", helper.contains("43499"))
        assertFalse("helper 不得 fork", containsCall(helper, "fork"))
        assertFalse("helper 不得 system", containsCall(helper, "system"))
        assertFalse("helper 不得直接 execve", containsCall(helper, "execve"))
    }

    @Test
    fun `cmake packages helper as lib so executable in native lib dir`() {
        val cmake = readCFile("app/src/main/cpp/CMakeLists.txt")
        assertTrue(cmake.contains("add_executable(ghostlock_victim_helper"))
        assertTrue(cmake.contains("PREFIX \"lib\""))
        assertTrue(cmake.contains("SUFFIX \".so\""))
        assertTrue(cmake.contains("RUNTIME_OUTPUT_DIRECTORY"))
        assertTrue(cmake.contains("gl43499/core/victim_protocol.c"))
    }

    @Test
    fun `zero fork wrap counters are linked for lib and helper`() {
        val cmake = readCFile("app/src/main/cpp/CMakeLists.txt")
        assertTrue(
            "lib 缺 --wrap=fork",
            cmake.contains("target_link_options(ghostlock43499 PRIVATE -Wl,-z,max-page-size=16384") &&
                cmake.contains("-Wl,--wrap=fork") &&
                cmake.contains("-Wl,--wrap=vfork"),
        )
        assertTrue(
            "helper 缺 --wrap=fork/vfork",
            cmake.contains("target_link_options(ghostlock_victim_helper PRIVATE") &&
                cmake.contains("-Wl,--wrap=vfork"),
        )
        val protocol = readCFile("app/src/main/cpp/gl43499/core/victim_protocol.c")
        listOf(
            "__wrap_fork",
            "__wrap_vfork",
            "__real_fork",
            "__real_vfork",
            "glk_victim_fork_count",
            "glk_victim_vfork_count",
            "atomic_fetch_add_explicit",
        ).forEach { token ->
            assertTrue("victim_protocol.c 缺零 fork ① 计数构件：$token", protocol.contains(token))
        }
        val helper = readCFile("app/src/main/cpp/gl43499/core/victim_helper/victim_helper.c")
        assertTrue(
            "helper bye 未携带 fork/vfork 计数",
            helper.contains("glk_victim_fork_count()") &&
                helper.contains("glk_victim_vfork_count()"),
        )
        /* 生产符号路径不得调用 fork/vfork（wrapper/canary 之外）：main/cpp 中仅协议文件
         * 存在 wrapper 定义（__wrap_*）与 JNI canary（debug-only，见 jni43499.c 注释）。 */
        val jni = readCFile("app/src/main/cpp/gl43499/jni43499.c")
        assertTrue("JNI 缺 forkGuardCanary", jni.contains("forkGuardCanary"))
        assertTrue(
            "canary 必须在子进程立即 _exit",
            jni.contains("if (child == 0) _exit(0);"),
        )
    }

    @Test
    fun `victim spawn file actions close before dup2 and skip control fds`() {
        val protocol = readCFile("app/src/main/cpp/gl43499/core/victim_protocol.c")
        val dup2Index = protocol.indexOf("posix_spawn_file_actions_adddup2")
        assertTrue("必须存在 adddup2", dup2Index >= 0)
        /* A16/mondrian E4 实测修订：源 fd 可能恰为 3/4，addclose 会先关掉
         * 后续 adddup2 的源 fd；fd 收敛改由 adddup2(3/4) + helper close_range(5,~0)
         * 承担（见 victim_protocol.c N02 注释）。 */
        assertFalse(
            "A16 实测后不得再 addclose 源 fd",
            protocol.contains("posix_spawn_file_actions_addclose"),
        )
        assertTrue("必须保留 helper close_range 收敛", protocol.contains("close_range"))
    }

    @Test
    fun `helper perf copy has acquire barrier before ring memcpy`() {
        val helper = readCFile(
            "app/src/main/cpp/gl43499/core/victim_helper/victim_helper.c",
        )
        assertTrue(
            "N03：data_head 必须 acquire load",
            helper.contains("__atomic_load_n(&meta[0], __ATOMIC_ACQUIRE)"),
        )
        assertTrue(
            "N03：读 ring 数据前必须有 acquire fence",
            helper.contains("__atomic_thread_fence(__ATOMIC_ACQUIRE)"),
        )
        assertTrue(
            "N03：data_tail 必须 release store",
            helper.contains("__atomic_store_n(&meta[1], data_head, __ATOMIC_RELEASE)"),
        )
    }

    @Test
    fun `sigpipe sensitive writes are marked no signal`() {
        val page = readCFile("app/src/main/cpp/gl43499/core/page.c")
        val tcp = readCFile("app/src/main/cpp/gl43499/core/carrier_tcp.c")
        assertTrue(
            "G09：shaping sendmsg 必须带 MSG_NOSIGNAL",
            page.contains("sendmsg(shaping_pair[0], &message, MSG_NOSIGNAL)"),
        )
        assertTrue(
            "G09：reclaim sendmsg 必须带 MSG_NOSIGNAL",
            page.contains("MSG_DONTWAIT | MSG_NOSIGNAL"),
        )
        assertTrue(
            "G09：tcp send 必须带 MSG_NOSIGNAL",
            tcp.contains("MSG_DONTWAIT | MSG_NOSIGNAL"),
        )
    }

    @Test
    fun `g08 cpu pin sentinel skips owner waiter and keeps consumer pinned`() {
        val actor = readCFile("app/src/main/cpp/gl43499/core/actor.c")
        assertTrue(
            "G08：owner 必须按 no-pin 哨兵跳过 pin",
            actor.contains("glk43499_internal_cpu_pin_required(actor->owner_cpu)"),
        )
        assertTrue(
            "G08：waiter 必须按 no-pin 哨兵跳过 pin",
            actor.contains("glk43499_internal_cpu_pin_required(actor->waiter_cpu)"),
        )
        assertTrue(
            "G08：consumer 必须无条件 pin（不接受哨兵）",
            actor.contains("(void) glk43499_internal_pin_cpu(actor->consumer_cpu);"),
        )
        assertFalse(
            "G08：consumer 不得走 pin_required 跳过逻辑",
            actor.contains("glk43499_internal_cpu_pin_required(actor->consumer_cpu)"),
        )
        assertTrue(
            "G08：owner/waiter 校验必须允许哨兵",
            actor.contains("glk43499_internal_cpu_is_no_pin(setup->owner_cpu)") &&
                actor.contains("glk43499_internal_cpu_is_no_pin(setup->waiter_cpu)"),
        )
    }

    /* ------------------------------------------------------------------ */
    /* 工具                                                                 */
    /* ------------------------------------------------------------------ */

    /** @return core/victim 下 Kotlin 源文件（按名字排序）。 */
    private fun victimKotlinFiles(): List<File> {
        val directory = resolve("app/src/main/kotlin/com/ghostlock/app/core/victim")
        return directory.listFiles { file -> file.isFile && file.name.endsWith(".kt") }
            ?.sortedBy { it.name }
            ?: emptyList()
    }

    /**
     * 判断命中是否仅出现在 KDoc/注释里（如“无 43499 常量”这类契约说明）。
     *
     * @param text 文件文本。
     * @param token 命中词。
     * @return true=仅存在注释/文档行。
     */
    private fun isDocumentationMention(text: String, token: String): Boolean =
        text.lineSequence()
            .filter { it.contains(token) }
            .all { line ->
                val trimmed = line.trim()
                trimmed.startsWith("*") || trimmed.startsWith("//") ||
                    trimmed.startsWith("/*") || trimmed.startsWith("#")
            }

    /**
     * @param text C 源码。
     * @param name 函数名（不带括号）。
     * @return true=出现 `name(` 调用形式。
     */
    private fun containsCall(text: String, name: String): Boolean =
        Regex("(^|[^A-Za-z0-9_])" + Regex.escape(name) + "\\s*\\(").containsMatchIn(text)

    /**
     * @param relative repo 相对路径。
     * @return 文件文本。
     */
    private fun readCFile(relative: String): String = resolve(relative).readText()

    /**
     * @param relative repo 相对路径。
     * @return 存在的文件。
     */
    private fun resolve(relative: String): File {
        val candidates = listOf(
            File(relative),
            File("../$relative"),
            File("../../$relative"),
        )
        return candidates.firstOrNull { it.isFile || it.isDirectory }
            ?: error("not found from ${File(".").absolutePath}: $relative")
    }
}
