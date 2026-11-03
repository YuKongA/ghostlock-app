package com.ghostlock.app.backend.cve43499

import com.ghostlock.app.jni.Native43499Jni
import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * P3 Native/JNI 机械卫生契约（task-77，B11–B17 + L5–L8）——**先写测试、不运行**。
 *
 * 纯 host/JVM 源码契约（host-parseable）：不加载 .so、不编译、不触 Android/真机；
 * 编译/运行时真值待后续 build 批。逐项对着审计编号断言。
 */
class NativeHygieneContractTest {

    @Test
    fun `B11 pthread create uses return value not errno`() {
        val actor = readRepoFile("app/src/main/cpp/gl43499/core/actor.c")
        val tcp = readRepoFile("app/src/main/cpp/gl43499/core/carrier_tcp.c")
        assertTrue(actor.contains("B11"))
        assertTrue(tcp.contains("B11"))
        assertTrue(actor.contains("create_status = pthread_create("))
        assertTrue(tcp.contains("create_status = pthread_create("))
        assertFalse("actor must not read errno after pthread_create",
            actor.contains("errno != 0 ? errno : EAGAIN"))
        assertFalse("tcp must not read errno after pthread_create",
            tcp.contains("errno != 0 ? errno : EAGAIN"))
    }

    @Test
    fun `B12 multicast trigger has no wall clock dependency in window`() {
        val source = readRepoFile("app/src/main/cpp/gl43499/core/carrier_multicast.c")
        val trigger = slice(source, "void glk43499_carrier_multicast_trigger(",
            "int32_t glk43499_carrier_multicast_disarm(")
        assertTrue(trigger.contains("B12"))
        assertFalse(trigger.contains("glk43499_internal_deadline"))
        assertFalse(trigger.contains("glk43499_internal_expired"))
        assertFalse(trigger.contains("clock_gettime"))
    }

    @Test
    fun `original 43499 JNI target is disabled by default`() {
        val cmake = readRepoFile("app/src/main/cpp/CMakeLists.txt")
        val option = cmake.indexOf("option(GHOSTLOCK_ENABLE_ORIGINAL_BACKENDS")
        val enabledBlock = cmake.indexOf("if(GHOSTLOCK_ENABLE_ORIGINAL_BACKENDS)", option)
        val target = cmake.indexOf("add_library(ghostlock43499 SHARED", enabledBlock)
        val blockEnd = cmake.indexOf("endif()", target)
        assertTrue(option >= 0)
        assertTrue(enabledBlock > option)
        assertTrue(target > enabledBlock)
        assertTrue(blockEnd > target)
        assertTrue(cmake.substring(option, enabledBlock).contains(" OFF)"))
    }

    @Test
    fun `F1 actor execute waits ready before post-ready settle`() {
        val source = readRepoFile("app/src/main/cpp/gl43499/core/actor.c")
        val block = slice(source, "/* TODO(task-131)",
            "/* 打开窗口：先发布 execute_requested")
        val readyIndex = block.indexOf("atomic_load(&impl->shared.waiter_waiting) == 0")
        val readyLoopIndex = block.indexOf("glk43499_internal_expired(ready_deadline)")
        val f1MarkerIndex = block.indexOf("/* task-131 F1")
        val settleIndex = block.indexOf(
            "usleep((useconds_t) impl->setup_settle_us);",
            f1MarkerIndex,
        )
        assertTrue("task-131 marker/ready wait must be present",
            readyIndex >= 0 && readyLoopIndex > readyIndex)
        assertTrue("post-ready settle marker must be after ready wait",
            f1MarkerIndex > readyLoopIndex)
        assertTrue("post-ready settle must be after marker", settleIndex > f1MarkerIndex)
        assertTrue(block.contains("race.cpp:374-378"))
    }

    @Test
    fun `B15 scan and attempt counts are bounded to int32 or u32 domains`() {
        val placement = readRepoFile("app/src/main/cpp/gl43499/core/placement.c")
        assertTrue(placement.contains("scan_round_limit > (uint64_t) UINT32_MAX"))
        val carriers = listOf(
            "carrier_multicast.c",
            "carrier_pselect.c",
            "carrier_tcp.c",
        ).map { readRepoFile("app/src/main/cpp/gl43499/core/$it") }
        carriers.forEach { carrier ->
            assertTrue(carrier.contains("operation_budget > (uint64_t) INT32_MAX"))
        }
    }

    @Test
    fun `B16 pselect retained is only set when resources actually remain`() {
        val source = readRepoFile("app/src/main/cpp/gl43499/core/carrier_pselect.c")
        val block = slice(source,
            "if (glk43499_pselect_backup_targets(window, table) == 0) {",
            "if (glk43499_pselect_install_targets(")
        assertTrue(block.contains("B16"))
        val elseIndex = block.indexOf("} else {")
        val retainedIndex = block.indexOf("window->retained = 1;")
        assertTrue("retained must be inside retained(clean==0) branch",
            elseIndex >= 0 && retainedIndex > elseIndex)
    }

    @Test
    fun `B17 reclaim fds are written to page before abort and closeable after`() {
        val source = readRepoFile("app/src/main/cpp/gl43499/core/page.c")
        val setIndex = source.indexOf("page->reclaim_read = reclaim_pair[0];")
        val abortRiskIndex = source.indexOf("setsockopt(reclaim_pair[1], SOL_SOCKET, SO_SNDBUF")
        assertTrue("B17 page reclaim identity must be set before later aborts",
            setIndex in 0..<abortRiskIndex)
        assertTrue(source.contains("int32_t shaping[2], int32_t reclaim[2]"))
        assertTrue(source.contains("page->reclaim_write = reclaim[1];"))
        assertTrue(source.contains("B17"))
    }

    @Test
    fun `L5 close and cancel entries reject handle zero before result class lookup`() {
        val source = readRepoFile("app/src/main/cpp/gl43499/jni43499.c")
        for (function in listOf(
            "Java_com_ghostlock_app_jni_Native43499Jni_cancelPlacement(",
            "Java_com_ghostlock_app_jni_Native43499Jni_closePlacement(",
            "Java_com_ghostlock_app_jni_Native43499Jni_cancelActor(",
            "Java_com_ghostlock_app_jni_Native43499Jni_closeActor(",
        )) {
            val block = slice(source, function, "JNIEXPORT")
            assertTrue("$function must check handle==0 first",
                block.contains("== 0) return GLK43499_JNI_NOT_ACCEPTED_INT;"))
        }
    }

    @Test
    fun `L6 close and cancel share one normalized bool writer`() {
        val source = readRepoFile("app/src/main/cpp/gl43499/jni43499.c")
        val writer = slice(source, "static int32_t glk43499_write_close_result(",
            "/**\n * @brief 读取一个非空 UTF-8 字符串")
        assertTrue(writer.contains("L6"))
        for (field in listOf(
            "stop_requested",
            "workers_joined",
            "owned_chain_released",
            "retained_for_process_exit",
        )) {
            assertTrue("$field must be normalized to 0/1",
                writer.contains("$field != 0 ? 1U : 0U"))
        }
        assertTrue(source.contains("glk43499_jni_placement_close_call"))
        assertTrue(source.contains("glk43499_jni_actor_close_call"))
    }

    @Test
    fun `L7 tri state sentinels have one C source and Kotlin parity`() {
        val source = readRepoFile("app/src/main/cpp/gl43499/jni43499.c")
        assertTrue(source.contains("_Static_assert(GLK43499_JNI_NOT_ACCEPTED_INT == -2"))
        assertTrue(source.contains("_Static_assert(GLK43499_JNI_INCOMPLETE_INT == INT32_MIN"))
        assertTrue(source.contains("_Static_assert(GLK43499_JNI_NOT_ACCEPTED_HANDLE == -2"))
        assertTrue(source.contains("_Static_assert(GLK43499_JNI_INCOMPLETE_HANDLE == -1"))
        assertEquals(-2, Native43499Jni.RESULT_NOT_ACCEPTED_INT)
        assertEquals(Int.MIN_VALUE, Native43499Jni.RESULT_INCOMPLETE_INT)
        assertEquals(-2L, Native43499Jni.RESULT_NOT_ACCEPTED_HANDLE)
        assertEquals(-1L, Native43499Jni.RESULT_INCOMPLETE_HANDLE)
    }

    @Test
    fun `L8 priority is read as explicit int32 and domain checked at JNI`() {
        val source = readRepoFile("app/src/main/cpp/gl43499/jni43499.c")
        assertTrue(source.contains("glk43499_get_int32"))
        assertTrue(source.contains("\"ownerPriority\", &owner_priority"))
        assertTrue(source.contains("setup.owner_priority < PRIO_MIN"))
        assertTrue(source.contains("setup.consumer_priority >= PRIO_MAX"))
        assertFalse("old uint32 priority cast path must be gone",
            source.contains("(int32_t) owner_priority"))
    }

    /**
     * @param source 文本。
     * @param start 起始锚点（含）。
     * @param end 结束锚点（不含）。
     * @return 区间文本；任一缺失即 fail。
     */
    private fun slice(source: String, start: String, end: String): String {
        val startIndex = source.indexOf(start)
        assertTrue("missing start anchor: $start", startIndex >= 0)
        val endIndex = source.indexOf(end, startIndex + start.length)
        assertTrue("missing end anchor: $end", endIndex > startIndex)
        return source.substring(startIndex, endIndex)
    }

    /** @param relative 相对仓库根路径。 @return 文件文本。 */
    private fun readRepoFile(relative: String): String {
        val candidates = listOf(File(relative), File("../$relative"), File("../../$relative"))
        return (candidates.firstOrNull { it.isFile } ?: error("missing repo file: $relative"))
            .readText()
    }
}
