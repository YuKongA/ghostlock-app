package com.ghostlock.app.engine.pagecache

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * 复原路径的三条判据（本批唯一新增的可执行约束）。
 *
 * 1. 只写 [TypedBackendProbe.RESTORE_BYTES]，offset 从 0 起按 16 递增；
 * 2. 复原**不**调用模块 precheck（fake 计数必须为 0）；
 * 3. 任一块失败 ⇒ 仍然调用既有 restoreHook，并返回 false。
 *
 * 参照系：本仓没有复原路径的先例（ref 的终点恢复只写钩子站点，见 CarrierRestore 的 KDoc），
 * 所以这三条就是这条补救路径的唯一边界；写原语本身仍由 [com.ghostlock.app.jni.cve43284.Assembled43284]
 * 的真机路径覆盖，这里只用 fake 把它与复原顺序隔开。
 */
class CarrierRestoreTest {

    /** 记录每次写的 offset/内容，并按要求计数 precheck 与 restoreHook。 */
    private class RecordingSession(
        private val failAt: Int? = null,
        private val order: MutableList<String>? = null,
    ) : CarrierRestore.Session {
        val writes = mutableListOf<Pair<Long, ByteArray>>()
        var precheckCalls = 0
        var closed = 0
        var head = byteArrayOf(0x7f, 0x45, 0x4c, 0x46)

        override fun precheckModule(image: ByteArray): Boolean {
            precheckCalls += 1
            return false
        }

        override fun writeBlock(offset: Long, block: ByteArray): Boolean {
            val index = writes.size
            writes += offset to block.copyOf()
            return failAt == null || index != failAt
        }

        override fun readHead(): ByteArray = head.copyOf()

        override fun close() {
            closed += 1
            order?.add("close")
        }
    }

    @Test
    fun onlyTheWrittenPrefixIsRestoredFromOffsetZeroInSixteenByteSteps() {
        /* 来源是一整份 system 副本（91088 B）；复原只允许碰我们写过的前 23512 B。 */
        val source = ByteArray(91088) { index -> (index % 251).toByte() }
        val fake = RecordingSession()
        val log = mutableListOf<String>()

        val ok = CarrierRestore { line -> log += line }.run(
            source = { source.copyOf() },
            session = { fake },
            restoreHook = { true },
        )

        assertTrue("块全部写完且头四字节是 ELF magic ⇒ true", ok)
        assertEquals("块数 = ceil(23512 / 16)", 1470, fake.writes.size)
        fake.writes.forEachIndexed { index, write ->
            assertEquals("第 " + index + " 块的 offset 必须从 0 按 16 递增", index * 16L, write.first)
            assertEquals("每块都是 16 字节（末块补齐）", 16, write.second.size)
            val at = index * 16
            val want = source.copyOfRange(at, minOf(at + 16, TypedBackendProbe.RESTORE_BYTES)).copyOf(16)
            assertTrue("第 " + index + " 块的内容必须来自原厂来源", write.second.contentEquals(want))
        }

        val planned = CarrierRestore.plan(source, TypedBackendProbe.RESTORE_BYTES)
        assertEquals("只复原我们写过的 23512 字节", TypedBackendProbe.RESTORE_BYTES, planned.sumOf { it.length })
        assertEquals("末块起点 = 23504", 23504L, planned.last().offset)
        assertEquals("末块只有 8 字节来自原厂", 8, planned.last().length)
        assertTrue(
            "写前/写后的头四字节在同一行",
            log.contains("restore: head before=7f454c46 after=7f454c46 (expect 7f454c46)"),
        )
        assertEquals("复原会话必须关掉", 1, fake.closed)
    }

    @Test
    fun theRestorePathNeverCallsTheModulePrecheck() {
        val fake = RecordingSession()
        val log = mutableListOf<String>()

        CarrierRestore { line -> log += line }.run(
            source = { ByteArray(TypedBackendProbe.RESTORE_BYTES) { 0x90.toByte() } },
            session = { fake },
            restoreHook = { true },
        )

        assertEquals("复原不得调用模块 precheck", 0, fake.precheckCalls)
        assertEquals(
            "mode 行必须是第一行",
            "restore-mode: precheck bypassed (carrier restore)",
            log.first(),
        )
        assertTrue(
            "计划闸门同样显式跳过",
            log.contains("restore-mode: plan gate bypassed (carrier restore)"),
        )
    }

    @Test
    fun aFailedBlockStillRestoresTheHookAndReturnsFalse() {
        val order = mutableListOf<String>()
        val fake = RecordingSession(failAt = 3, order = order)
        var hookCalls = 0
        val log = mutableListOf<String>()

        val ok = CarrierRestore { line -> log += line }.run(
            source = { ByteArray(TypedBackendProbe.RESTORE_BYTES) { 0x90.toByte() } },
            session = { fake },
            restoreHook = {
                order += "hook"
                hookCalls += 1
                true
            },
        )

        assertFalse("有块没写成 ⇒ false", ok)
        assertEquals("失败块（含失败那次）之后不再写：0..3 共四次尝试", 4, fake.writes.size)
        assertEquals("无论成败都要调用既有 restoreHook", 1, hookCalls)
        assertTrue("钩子还原的结果单独一行", log.contains("restore: hook restored=true"))
        assertTrue("固定收尾行必须带字节数与结论", log.contains("restore: bytes=23512 ok=false"))
        assertEquals("失败路径也要关掉会话", 1, fake.closed)
        assertEquals("先关会话（让出 ESP socket 的固定端口）再还原钩子", listOf("close", "hook"), order)
    }
}
