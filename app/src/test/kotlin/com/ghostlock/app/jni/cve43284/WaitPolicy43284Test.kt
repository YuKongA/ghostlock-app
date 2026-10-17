package com.ghostlock.app.jni.cve43284

import org.junit.Assert.assertEquals
import org.junit.Test

/**
 * wait 的统一口径与双判据（本批四项之二）。
 *
 * 参照 ghostlock-app-ref@5f184760 的：
 *  - src/core/backend/cve_2026_43284_state.hpp:29-31（15000 / 40 / 5）；
 *  - src/core/backend/cve_2026_43284/real_ops.cpp:408-414（备用标记参与判定）；
 *  - src/core/backend/cve_2026_43284/real_ops.hpp:60-72（四个标记的路径）。
 */
class WaitPolicy43284Test {
    private class Recorder : WaitPolicy43284.TerminalProbe {
        val calls = mutableListOf<String>()
        var answers: MutableList<Long> = mutableListOf()

        override fun await(budgetMs: Long, pollMs: Long, successMarker: String, failureMarker: String): Long {
            calls += budgetMs.toString() + "/" + pollMs + "/" + successMarker + "/" + failureMarker
            return if (answers.isEmpty()) WaitPolicy43284.TERMINAL_INFLIGHT else answers.removeAt(0)
        }
    }

    @Test
    fun theTripleIsTheReferenceTriple() {
        assertEquals(15_000L, WaitPolicy43284.TIMEOUT_MS)
        assertEquals(40L, WaitPolicy43284.POLL_MS)
        assertEquals(5L, WaitPolicy43284.POLL_ATTEMPTS)
        assertEquals("备用窗口 = 5 次 × 40 ms", 200L, WaitPolicy43284.ALT_BUDGET_MS)
        assertEquals("/data/local/tmp/.ghostlock_lkm_ok", WaitPolicy43284.SUCCESS_MARKER)
        assertEquals("/dev/dfm0", WaitPolicy43284.SUCCESS_MARKER_ALT)
        assertEquals("/data/local/tmp/.ghostlock_lkm_fail", WaitPolicy43284.FAILURE_MARKER)
        assertEquals("/dev/dfm1", WaitPolicy43284.FAILURE_MARKER_ALT)
        assertEquals("/dev/df", WaitPolicy43284.IN_FLIGHT_MARKER)
        assertEquals("/sys/module/dirtyfrag", WaitPolicy43284.MODULE_PATH)
    }

    @Test
    fun theReadableMarkersAreTheFirstJudge() {
        val recorder = Recorder().apply { answers = mutableListOf(WaitPolicy43284.TERMINAL_SUCCESS) }
        val lines = mutableListOf<String>()
        val terminal = WaitPolicy43284.await(recorder, { line -> lines += line })
        assertEquals(WaitPolicy43284.TERMINAL_SUCCESS, terminal)
        assertEquals(
            "第一条判据必须是可读标记那一组，且带统一预算/节拍",
            listOf("15000/40//data/local/tmp/.ghostlock_lkm_ok//data/local/tmp/.ghostlock_lkm_fail"),
            recorder.calls,
        )
        assertEquals(1, lines.size)
    }

    @Test
    fun theDeviceMarkersAreTheSecondJudge() {
        val recorder = Recorder().apply {
            answers = mutableListOf(WaitPolicy43284.TERMINAL_INFLIGHT, WaitPolicy43284.TERMINAL_SUCCESS)
        }
        val terminal = WaitPolicy43284.await(recorder, { _ -> })
        assertEquals("任一命中即成功", WaitPolicy43284.TERMINAL_SUCCESS, terminal)
        assertEquals(
            listOf(
                "15000/40//data/local/tmp/.ghostlock_lkm_ok//data/local/tmp/.ghostlock_lkm_fail",
                "200/40//dev/dfm0//dev/dfm1",
            ),
            recorder.calls,
        )
    }

    @Test
    fun aFailureFromTheSecondJudgeIsAFailureNotATimeout() {
        val recorder = Recorder().apply {
            answers = mutableListOf(WaitPolicy43284.TERMINAL_INFLIGHT, WaitPolicy43284.TERMINAL_FAILURE)
        }
        assertEquals(WaitPolicy43284.TERMINAL_FAILURE, WaitPolicy43284.await(recorder, { _ -> }))
    }

    @Test
    fun nothingSeenStaysInflight() {
        val recorder = Recorder()
        val lines = mutableListOf<String>()
        val terminal = WaitPolicy43284.await(recorder, { line -> lines += line })
        assertEquals(WaitPolicy43284.TERMINAL_INFLIGHT, terminal)
        assertEquals(2, recorder.calls.size)
        assertEquals(2, lines.size)
    }
}
