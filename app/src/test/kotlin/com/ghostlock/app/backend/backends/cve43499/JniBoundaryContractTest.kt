package com.ghostlock.app.backend.backends.cve43499

import com.ghostlock.app.jni.Native43499MulticastSpec
import com.ghostlock.app.jni.Native43499PselectSpec
import com.ghostlock.app.jni.Native43499PrimitiveSpec
import com.ghostlock.app.jni.Native43499TcpZerocopySpec
import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * JNI 边界契约（P2/L1–L4，task-74）——**按用户令先写测试、不运行**。
 *
 * 纯 host/JVM：Kotlin DTO 反射 + `jni43499.c` 源码结构断言（host-parseable）；
 * 不加载 libghostlock43499.so、不触 Android/JNI/真机。JNI 行为真值仍以编译后
 * fake/instrumented 契约（另批）为准。
 */
class JniBoundaryContractTest {

    @Test
    fun `L1 primitive kind is explicit kotlin field and jni does not sniff classes`() {
        val jni = readRepoFile("app/src/main/cpp/backends/43499/jni43499.c")
        val kotlin = readRepoFile("app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt")

        /* JNI 不得再按 JVM 类名判 primitive kind（result DTO 的 IsInstanceOf 校验不在此列）。 */
        assertFalse("JNI must not keep class-name sniffer helper",
            jni.contains("glk43499_is_instance"))
        for (className in listOf(
            "Native43499MulticastSpec",
            "Native43499PselectSpec",
            "Native43499TcpZerocopySpec",
        )) {
            assertFalse("JNI must not reference primitive class $className",
                jni.contains(className))
        }
        assertTrue("JNI must read primitiveKind field", jni.contains("\"primitiveKind\""))
        assertTrue(jni.contains("GLK43499_PRIMITIVE_MULTICAST_SENDMSG"))
        assertTrue(jni.contains("GLK43499_PRIMITIVE_TCP_ZEROCOPY_RECEIVE"))
        assertTrue(
            "unknown/out-of-range primitiveKind must fail closed",
            jni.contains("primitive_kind > (uint32_t) GLK43499_PRIMITIVE_TCP_ZEROCOPY_RECEIVE"),
        )

        /* 三个 sealed 实现都显式携带 primitiveKind。 */
        for (className in listOf(
            "Native43499MulticastSpec",
            "Native43499PselectSpec",
            "Native43499TcpZerocopySpec",
        )) {
            val block = kotlin
                .substringAfter("internal data class $className(")
                .substringBefore(") : Native43499PrimitiveSpec")
            assertTrue("$className missing primitiveKind", block.contains("primitiveKind: Int"))
        }

        assertEquals(1, Native43499PrimitiveSpec.KIND_MULTICAST_SENDMSG)
        assertEquals(2, Native43499PrimitiveSpec.KIND_PSELECT)
        assertEquals(3, Native43499PrimitiveSpec.KIND_TCP_ZEROCOPY_RECEIVE)
        assertEquals(
            Int::class.javaPrimitiveType,
            Native43499MulticastSpec::class.java.getDeclaredField("primitiveKind").type,
        )
        assertEquals(
            Int::class.javaPrimitiveType,
            Native43499PselectSpec::class.java.getDeclaredField("primitiveKind").type,
        )
        assertEquals(
            Int::class.javaPrimitiveType,
            Native43499TcpZerocopySpec::class.java.getDeclaredField("primitiveKind").type,
        )
    }

    @Test
    fun `L2 translation failure returns not accepted without writing receipt`() {
        val jni = readRepoFile("app/src/main/cpp/backends/43499/jni43499.c")

        /* perf open 翻译失败：清异常 + NOT_ACCEPTED；不得走写回执 reject helper。 */
        val perfOpen = jni.substringAfter("Native43499Jni_openPerfSampler(")
            .substringBefore("Java_com_ghostlock_app_jni_Native43499Jni_collectPerfSamples")
        assertTrue(perfOpen.contains("glk43499_jni_clear_pending(env);"))
        assertTrue(perfOpen.contains("return GLK43499_JNI_NOT_ACCEPTED_HANDLE;"))
        assertFalse(perfOpen.contains("glk43499_jni_reject_invalid_argument"))

        /* victim spawn 翻译失败（解析 false）：同样不写回执；raw_size 参数域失败保留
         * INVALID_ARGUMENT（不属于翻译失败）。 */
        val spawnParsed = jni.substringAfter("const int32_t parsed =")
            .substringBefore("struct glk_victim_spawn_request request;")
        assertFalse(spawnParsed.contains("glk43499_jni_reject_invalid_argument"))
        assertTrue(spawnParsed.contains("glk43499_jni_clear_pending(env);"))
        assertTrue(jni.contains("glk43499_jni_reject_invalid_argument(env, result_object);"))
        val rawSizeBlock = jni.substringAfter("if (raw_size < (jlong) GLK_VICTIM_RAW_HEADER_BYTES) {")
            .substringBefore("}")
        assertTrue(rawSizeBlock.contains("glk43499_jni_reject_invalid_argument(env, result_object);"))

        /* host-parseable 替代 fake core 调用计数：翻译失败分支不得调用 core。 */
        val perfOpenBeforeCore = jni
            .substringAfter("Native43499Jni_openPerfSampler(")
            .substringBefore("const glk43499_handle handle = glk43499_perf_open(")
        assertFalse(perfOpenBeforeCore.contains("glk43499_perf_open("))
        val prepareActorBeforeCore = jni
            .substringAfter("Native43499Jni_prepareActor(")
            .substringBefore("const glk43499_handle actor = glk43499_prepare_actor(")
        assertFalse(prepareActorBeforeCore.contains("glk43499_prepare_actor("))
    }

    @Test
    fun `L3 empty array legal and oom or invalid classification`() {
        val jni = readRepoFile("app/src/main/cpp/backends/43499/jni43499.c")
        val copyBytes = jni
            .substringAfter("static glk43499_copy_status glk43499_copy_bytes(")
            .substringBefore("/** @brief 清零并释放调用期字节副本。 */")

        assertTrue(copyBytes.contains("if (length == 0) return GLK43499_COPY_OK;"))
        assertFalse("empty array must not be rejected as invalid", copyBytes.contains("length <= 0"))
        assertTrue(copyBytes.contains("GLK43499_COPY_NO_MEMORY"))
        assertTrue(copyBytes.contains("GLK43499_COPY_TRANSLATION_FAILED"))
        assertTrue(copyBytes.contains("if (source == NULL) return GLK43499_COPY_INVALID;"))
        assertTrue(copyBytes.contains("malloc((size_t) length)"))

        /* 调用方必须把 OOM/非法参数写成具名回执，翻译失败不写。 */
        assertTrue(jni.contains("GLK43499_CALL_SYSTEM_ERROR"))
        assertTrue(jni.contains("ENOMEM"))
        assertTrue(jni.contains("GLK43499_CALL_INVALID_ARGUMENT"))
    }

    @Test
    fun `L4 perf collect copies only validated bytes copied bounded by capacity`() {
        val jni = readRepoFile("app/src/main/cpp/backends/43499/jni43499.c")
        val perfCollect = jni
            .substringAfter("Native43499Jni_collectPerfSamples(")
            .substringBefore("JNIEXPORT jint JNICALL\nJava_com_ghostlock_app_jni_Native43499Jni_closePerfSampler(")

        assertTrue(perfCollect.contains("const uint64_t copied_bytes = result.receipt.bytes_copied;"))
        assertTrue(perfCollect.contains("copied_bytes > (uint64_t) capacity"))
        assertTrue(perfCollect.contains("glk43499_write_byte_array(env, out, (uint64_t) capacity, buffer,"))
        assertTrue(perfCollect.contains("GLK43499_CALL_INVALID_ARGUMENT"))
        assertFalse("JNI must not copy by core receipt data_size",
            perfCollect.contains("result.receipt.data_size"))
        assertTrue(perfCollect.contains("GLK43499_CALL_SYSTEM_ERROR"))
        assertTrue(perfCollect.contains("ENOMEM"))
    }

    /**
     * @param relative 相对仓库根路径。
     * @return 文件文本（从常见 cwd 候选查找）。
     */
    private fun readRepoFile(relative: String): String {
        val candidates = listOf(File(relative), File("../$relative"), File("../../$relative"))
        return (candidates.firstOrNull { it.isFile } ?: error("missing repo file: $relative"))
            .readText()
    }
}
