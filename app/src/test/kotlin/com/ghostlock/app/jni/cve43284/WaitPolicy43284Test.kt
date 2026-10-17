package com.ghostlock.app.jni.cve43284

import org.junit.Assert.assertEquals
import org.junit.Test

/**
 * wait 判据的单循环与顺序（照 ghostlock-app-ref@5f184760 的 real_ops.cpp:400-420 与 state.hpp:29-31）。
 */
class WaitPolicy43284Test {
    private class Recorder : WaitPolicy43284.TerminalProbe {
        val calls = mutableListOf<String>()
        var answers: MutableList<Long> = mutableListOf()

        override fun await(budgetMs: Long, pollMs: Long, successMarker: String, failureMarker: String): Long {
            calls += budgetMs.toString() + "/" + successMarker + "/" + failureMarker
            return if (answers.isEmpty()) WaitPolicy43284.TERMINAL_INFLIGHT else answers.removeAt(0)
        }
    }

    @Test
    fun theTripleIsTheReferenceTriple() {
        assertEquals(15_000L, WaitPolicy43284.TIMEOUT_MS)
        assertEquals(40L, WaitPolicy43284.POLL_MS)
        assertEquals(5L, WaitPolicy43284.POLL_ATTEMPTS)
        assertEquals("/data/local/tmp/.ghostlock_lkm_ok", WaitPolicy43284.SUCCESS_MARKER)
        assertEquals("/dev/dfm0", WaitPolicy43284.SUCCESS_MARKER_ALT)
        assertEquals("/data/local/tmp/.ghostlock_lkm_fail", WaitPolicy43284.FAILURE_MARKER)
        assertEquals("/dev/dfm1", WaitPolicy43284.FAILURE_MARKER_ALT)
        assertEquals("/sys/module/dirtyfrag", WaitPolicy43284.MODULE_PATH)
    }

    @Test
    fun theReadableJudgesFirstAndFailsBeforeSucceeding() {
        val recorder = Recorder().apply {
            answers = mutableListOf(WaitPolicy43284.TERMINAL_SUCCESS, WaitPolicy43284.TERMINAL_FAILURE)
        }
        val lines = mutableListOf<String>()
        val terminal = WaitPolicy43284.await(recorder, { line -> lines += line }, { false }, 4)
        assertEquals("备用判据的失败优先于主判据的成功", WaitPolicy43284.TERMINAL_FAILURE, terminal)
        assertEquals(2, recorder.calls.size)
        assertEquals(1, lines.size)
    }

    @Test
    fun thePrimarySuccessWinsWhenNothingFails() {
        val recorder = Recorder().apply {
            answers = mutableListOf(WaitPolicy43284.TERMINAL_SUCCESS, WaitPolicy43284.TERMINAL_INFLIGHT)
        }
        assertEquals(WaitPolicy43284.TERMINAL_SUCCESS, WaitPolicy43284.await(recorder, { _ -> }, { false }, 4))
        assertEquals(2, recorder.calls.size)
    }

    @Test
    fun theAlternateJudgesInTheSameRound() {
        val recorder = Recorder().apply {
            answers = mutableListOf(WaitPolicy43284.TERMINAL_INFLIGHT, WaitPolicy43284.TERMINAL_SUCCESS)
        }
        assertEquals(WaitPolicy43284.TERMINAL_SUCCESS, WaitPolicy43284.await(recorder, { _ -> }, { false }, 4))
        assertEquals(2, recorder.calls.size)
    }

    @Test
    fun theModuleBeingPresentIsSuccess() {
        val recorder = Recorder()
        assertEquals(WaitPolicy43284.TERMINAL_SUCCESS, WaitPolicy43284.await(recorder, { _ -> }, { true }, 3))
    }

    @Test
    fun nothingSeenStaysInflightAfterTheBudget() {
        val recorder = Recorder()
        val lines = mutableListOf<String>()
        val terminal = WaitPolicy43284.await(recorder, { line -> lines += line }, { false }, 3)
        assertEquals(WaitPolicy43284.TERMINAL_INFLIGHT, terminal)
        assertEquals(6, recorder.calls.size)
        assertEquals(1, lines.size)
    }
}
