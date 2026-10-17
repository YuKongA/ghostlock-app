package com.ghostlock.app.jni.cve43284

import org.junit.Assert.assertEquals
import org.junit.Test

/**
 * 序号自动接续的规则（本批四项之一）。
 *
 * 参照 ghostlock-app-ref@5f184760 的 src/core/backend/cve_2026_43284/real_ops.cpp:250-257 / 307-327 / 339-344：
 * 写成功之后计数器由 native 返回的序号接手，后续段只读它；查询拿不到值时**不许**把计数器按回 0
 * （那正是防重放丢包的形态）。
 */
class SequenceContinuationTest {
    private val lines = mutableListOf<String>()
    private val tracker = SequenceContinuation({ line -> lines += line })

    @Test
    fun theFirstWriteIsLoggedAsAnAutoHandoff() {
        assertEquals("链首没有值", 0L, tracker.value)
        assertEquals(4L, tracker.advance(4L))
        assertEquals(
            "日志行必须是固定形态：seq: <prev> -> <next> (auto)",
            listOf("seq: 0 -> 4 (auto)"),
            lines,
        )
    }

    @Test
    fun everyWriteAdvancesTheOneCounter() {
        tracker.advance(2L)
        tracker.advance(5L)
        tracker.advance(9L)
        assertEquals(9L, tracker.value)
        assertEquals(
            listOf("seq: 0 -> 2 (auto)", "seq: 2 -> 5 (auto)", "seq: 5 -> 9 (auto)"),
            lines,
        )
    }

    @Test
    fun aFailedQueryKeepsTheCounterInsteadOfResettingIt() {
        tracker.advance(7L)
        assertEquals("查询失败（0）⇒ 保持原值", 7L, tracker.advance(0L))
        assertEquals("负值同样按“没拿到”处理", 7L, tracker.advance(-3L))
        assertEquals(
            listOf(
                "seq: 0 -> 7 (auto)",
                "seq: 7 -> 7 (auto) query-refused=0",
                "seq: 7 -> 7 (auto) query-refused=-3",
            ),
            lines,
        )
    }

    @Test
    fun aSeededChainStartsWhereTheCallerSaid() {
        val seeded = SequenceContinuation({ line -> lines += line }, initial = 40L)
        assertEquals(40L, seeded.value)
        assertEquals(41L, seeded.advance(41L))
        assertEquals(listOf("seq: 40 -> 41 (auto)"), lines)
    }
}
