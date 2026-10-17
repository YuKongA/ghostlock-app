package com.ghostlock.app.jni.cve43284

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * trigger 形态的参数化（本批四项之三）：点火者与延迟集中在 TriggerSpec43284，失败保留**一次**重试。
 *
 * 参照 ghostlock-app-ref@5f184760 的 src/core/backend/cve_2026_43284/real_ops.cpp:355-379（点火者是参数）
 * 与 execution_binding.cpp:170（生产延迟 = 500）。
 */
class TriggerSpec43284Test {
    @Test
    fun theDefaultFormIs500msWithTheIgniterAsAParameter() {
        assertEquals(500L, TriggerSpec43284.DEFAULT_DELAY_MS)
        assertEquals(500L, TriggerSpec43284.DEFAULT.delayMs)
        assertEquals("/apex/com.android.runtime/bin/crash_dump32", TriggerSpec43284.DEFAULT.igniterPath)
        assertEquals(listOf("1", "1", "11", "1"), TriggerSpec43284.DEFAULT.igniterArgs)
        assertEquals("只保留一次重试", 1, TriggerSpec43284.DEFAULT.retries)
    }

    @Test
    fun afailedFirstAttemptIsRetriedExactlyOnce() {
        val attempts = mutableListOf<String>()
        val lines = mutableListOf<String>()
        val exit = detonate43284(TriggerSpec43284.DEFAULT, { line -> lines += line }) { path, arguments ->
            attempts += path + " " + arguments.joinToString(" ")
            if (attempts.size == 1) 1 else 0
        }
        assertEquals("第二次成功", 0, exit)
        assertEquals(2, attempts.size)
        assertTrue(lines.any { it.startsWith("detonate: attempt=2/2") })
    }

    @Test
    fun aSuccessfulFirstAttemptIsNotRepeated() {
        var calls = 0
        val exit = detonate43284(TriggerSpec43284.DEFAULT, { _ -> }) { _, _ ->
            calls += 1
            0
        }
        assertEquals(0, exit)
        assertEquals(1, calls)
    }

    @Test
    fun withoutAnIgniterNothingRuns() {
        var calls = 0
        val exit = detonate43284(
            TriggerSpec43284(delayMs = 500L, igniterPath = null),
            { _ -> },
        ) { _, _ ->
            calls += 1
            0
        }
        assertEquals("没有点火者 ⇒ -1（没有跑，不是成功）", -1, exit)
        assertEquals(0, calls)
    }
}
