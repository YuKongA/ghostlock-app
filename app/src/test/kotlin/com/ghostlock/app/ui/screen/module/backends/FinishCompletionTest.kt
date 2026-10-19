package com.ghostlock.app.ui.screen.module.backends

import java.util.concurrent.atomic.AtomicInteger
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

/** 完成信号的三态：出现在到无 / 超时 / 视图不可读。 */
class FinishCompletionTest {

    @Test
    fun seesAppearThenGone() {
        val sequence = listOf(true, true, false)
        val index = AtomicInteger(0)
        val result = FinishExecution.awaitCompletion(1_000L, 1L) {
            sequence[index.getAndIncrement().coerceAtMost(sequence.lastIndex)]
        }
        assertEquals(true, result)
    }

    @Test
    fun unreadableViewIsNull() {
        assertNull(FinishExecution.awaitCompletion(1_000L, 1L) { null })
    }

    @Test
    fun neverSeenIsTimeout() {
        assertEquals(false, FinishExecution.awaitCompletion(5L, 1L) { false })
    }
}
