package com.ghostlock.app.backend.cve43499.perf

import com.ghostlock.app.jni.Native43499CloseResult
import com.ghostlock.app.jni.Native43499PerfCollectResult
import com.ghostlock.app.jni.Native43499PerfOpenRequest
import com.ghostlock.app.jni.Native43499PerfOpenResult
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test

/**
 * Kotlin owner 合同测试（R4；H3-a 无状态 Native 的正确性证据）。
 *
 * Native 不检查 OPEN/COLLECTED/CLOSED；本测试证明 Kotlin owner 提供：
 * close 恰好一次、close 后 token 清零、close 后 collect/close 不再触 JNI、
 * collect 失败路径仍由 finally close。
 *
 * @note 纯 JVM；使用 fake Native 统计调用次数。
 */
class Cve43499PerfOwnerTest {

    @Test
    fun `open collect close calls native exactly once each`() {
        val native = CountingPerfNative()
        val owner = Cve43499PerfOwner.wrap(native, handle = 7L)
        assertEquals(0, native.openOrWrapCalls)
        val collected = Native43499PerfCollectResult()
        assertEquals(0, owner.collect(ByteArray(16), collected))
        assertEquals(1, native.collectCalls)
        val closeResult = Native43499CloseResult()
        assertEquals(0, owner.close(closeResult))
        assertEquals(1, native.closeCalls)
        assertEquals(0L, owner.tokenForTest())
    }

    @Test
    fun `second close is owner-local and does not touch native`() {
        val native = CountingPerfNative()
        val owner = Cve43499PerfOwner.wrap(native, 7L)
        assertEquals(0, owner.close(Native43499CloseResult()))
        assertEquals(
            Cve43499PerfOwner.OWNER_ALREADY_CLOSED,
            owner.close(Native43499CloseResult()),
        )
        assertEquals(1, native.closeCalls)
        assertEquals(0L, owner.tokenForTest())
    }

    @Test
    fun `not accepted close retains token`() {
        val native = CountingPerfNative(closeStatus = -2)
        val owner = Cve43499PerfOwner.wrap(native, 7L)
        assertEquals(-2, owner.close(Native43499CloseResult()))
        /* B8：NOT_ACCEPTED 表示 Native 未消费 token ⇒ owner 不清零。 */
        assertEquals(7L, owner.tokenForTest())
        assertEquals(1, native.closeCalls)
    }

    @Test
    fun `collect after close is rejected before jni`() {
        val native = CountingPerfNative()
        val owner = Cve43499PerfOwner.wrap(native, 7L)
        owner.close(Native43499CloseResult())
        try {
            owner.collect(ByteArray(16), Native43499PerfCollectResult())
            fail("collect after close must throw")
        } catch (expected: IllegalStateException) {
            assertTrue(expected.message!!.contains("collect after close"))
        }
        assertEquals(0, native.collectCalls)
    }

    @Test
    fun `collect failure still closes owner once`() {
        val native = CountingPerfNative(collectStatus = 8)
        val owner = Cve43499PerfOwner.wrap(native, 7L)
        assertEquals(8, owner.collect(ByteArray(16), Native43499PerfCollectResult()))
        owner.close(Native43499CloseResult())
        assertEquals(1, native.closeCalls)
        assertEquals(0L, owner.tokenForTest())
    }

    /** 统计调用的 fake Native；open 不被 owner 使用（wrap 直接包装已打开 token）。 */
    private class CountingPerfNative(
        private val collectStatus: Int = 0,
        private val closeStatus: Int = 0,
    ) : Cve43499PerfNative {
        var openOrWrapCalls: Int = 0
        var collectCalls: Int = 0
        var closeCalls: Int = 0

        override fun open(
            request: Native43499PerfOpenRequest,
            result: Native43499PerfOpenResult,
        ): Long {
            openOrWrapCalls++
            return 7L
        }

        override fun collect(
            handle: Long,
            out: ByteArray,
            result: Native43499PerfCollectResult,
        ): Int {
            collectCalls++
            result.callStatus = collectStatus
            return collectStatus
        }

        override fun close(handle: Long, result: Native43499CloseResult): Int {
            closeCalls++
            result.callStatus = closeStatus
            return closeStatus
        }
    }
}
