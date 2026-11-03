package com.ghostlock.app.core.flow

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * B1：`FlowHostParams` 类型/预算/Codec 契约（值层纯 JVM）。
 *
 * @note Bundle 真实 wire 需要 Robolectric/Android runtime；本项目 JVM（Gradle 固定
 *   JDK 25）下 Robolectric 4.17 无法启动（`FileDescriptor` internals 不兼容），
 *   因此 codec 逻辑以 [FlowHostParamsCodec.encodeValues]/`decodeValues` 值层覆盖；
 *   Bundle 搬运层属 B2 `:flowhost`/设备门禁，见报告 UNVERIFIED。
 */
class FlowHostParamsTest {
    @Test
    fun `whitelisted types roundtrip through codec values`() {
        val params = FlowHostParams.of(
            linkedMapOf(
                "s" to "value",
                "i" to 42,
                "l" to 42L,
                "b" to true,
                "f" to 1.5f,
                "d" to 2.5,
                "bytes" to byteArrayOf(1, 2, 3),
                "list" to listOf("a", "b"),
            ),
        )
        val decoded = FlowHostParamsCodec.decodeValues(FlowHostParamsCodec.encodeValues(params))
        assertEquals("value", decoded.getString("s"))
        assertEquals(42, decoded.getInt("i"))
        assertEquals(42L, decoded.getLong("l"))
        assertEquals(true, decoded.getBoolean("b"))
        assertEquals(1.5f, decoded.getFloat("f")!!, 0f)
        assertEquals(2.5, decoded.getDouble("d")!!, 0.0)
        assertArrayEquals(byteArrayOf(1, 2, 3), decoded.getByteArray("bytes"))
        assertEquals(listOf("a", "b"), decoded.getStringList("list"))
    }

    @Test
    fun `non whitelisted type is rejected`() {
        val error = runCatching {
            FlowHostParams.of(mapOf("obj" to Any()))
        }.exceptionOrNull()
        assertTrue(error is IllegalArgumentException)
        assertEquals("flow-params:unsupported-type:obj", error!!.message)
    }

    @Test
    fun `too many keys rejected`() {
        val values = LinkedHashMap<String, Any?>()
        repeat(FlowHostParams.MAX_KEYS + 1) { index -> values["k$index"] = index }
        val error = runCatching { FlowHostParams.of(values) }.exceptionOrNull()
        assertEquals("flow-params:too-many-keys", error!!.message)
    }

    @Test
    fun `long string rejected`() {
        val value = "x".repeat(FlowHostParams.MAX_STRING_BYTES + 1)
        val error = runCatching {
            FlowHostParams.of(mapOf("long" to value))
        }.exceptionOrNull()
        assertEquals("flow-params:string-too-long:long", error!!.message)
    }

    @Test
    fun `total budget rejected`() {
        val blob = ByteArray(FlowHostParams.MAX_TOTAL_BYTES / 2 + 1)
        val error = runCatching {
            FlowHostParams.of(mapOf("a" to blob, "b" to blob, "c" to blob))
        }.exceptionOrNull()
        assertEquals("flow-params:too-large", error!!.message)
    }

    @Test
    fun `decode rejects non whitelisted values`() {
        val serializableError = runCatching {
            FlowHostParamsCodec.decodeValues(mapOf("obj" to java.io.File("fixture")))
        }.exceptionOrNull()
        assertEquals("flow-params:unsupported-type:obj", serializableError!!.message)

        val arrayError = runCatching {
            FlowHostParamsCodec.decodeValues(mapOf("ints" to intArrayOf(1, 2)))
        }.exceptionOrNull()
        assertEquals("flow-params:unsupported-type:ints", arrayError!!.message)

        val mixedListError = runCatching {
            FlowHostParamsCodec.decodeValues(mapOf("list" to listOf("a", 1)))
        }.exceptionOrNull()
        assertEquals("flow-params:unsupported-type:list", mixedListError!!.message)
    }

    @Test
    fun `decode enforces budget`() {
        val error = runCatching {
            FlowHostParamsCodec.decodeValues(
                mapOf("blob" to ByteArray(FlowHostParams.MAX_TOTAL_BYTES + 1)),
            )
        }.exceptionOrNull()
        assertEquals("flow-params:too-large", error!!.message)
    }

    @Test
    fun `schema rejects unknown key`() {
        val params = FlowHostParams.of(mapOf("known" to 1, "unknown" to 2))
        val error = runCatching {
            params.validateSchema(setOf("known"))
        }.exceptionOrNull()
        assertEquals("flow-params:unknown-key:unknown", error!!.message)
    }

    @Test
    fun `schema accepts known keys`() {
        FlowHostParams.of(mapOf("known" to 1)).validateSchema(setOf("known", "other"))
    }
}
