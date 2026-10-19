package com.ghostlock.app.jni.cve43284

import org.junit.Assert.assertEquals
import org.junit.Test

/** wait 只认实际效果：su 或 module 存在；不认任何自定义 marker。 */
class WaitPolicy43284Test {
    private class Recorder : WaitPolicy43284.TerminalProbe {
        var calls = 0
        override fun await(budgetMs: Long, pollMs: Long, successMarker: String, failureMarker: String): Long {
            calls += 1
            return WaitPolicy43284.TERMINAL_INFLIGHT
        }
    }

    @Test
    fun theTripleIsTheReferenceTriple() {
        assertEquals(15_000L, WaitPolicy43284.TIMEOUT_MS)
        assertEquals(40L, WaitPolicy43284.POLL_MS)
        assertEquals(5L, WaitPolicy43284.POLL_ATTEMPTS)
        assertEquals("/system/bin/su", WaitPolicy43284.SUCCESS_PATH)
        assertEquals("/system/xbin/su", WaitPolicy43284.SUCCESS_PATH_ALT)
        assertEquals("/sys/module/kernelsu", WaitPolicy43284.MODULE_PATH)
        assertEquals("/sys/module/dirtyfrag", WaitPolicy43284.MODULE_PATH_ALT)
    }

    @Test
    fun suIsSuccess() {
        val recorder = Recorder()
        val terminal = WaitPolicy43284.await(
            recorder,
            { _ -> },
            { path -> path == WaitPolicy43284.SUCCESS_PATH },
            4,
        )
        assertEquals(WaitPolicy43284.TERMINAL_SUCCESS, terminal)
        assertEquals(0, recorder.calls)
    }

    @Test
    fun alternateSuIsSuccess() {
        val terminal = WaitPolicy43284.await(
            Recorder(),
            { _ -> },
            { path -> path == WaitPolicy43284.SUCCESS_PATH_ALT },
            4,
        )
        assertEquals(WaitPolicy43284.TERMINAL_SUCCESS, terminal)
    }

    @Test
    fun moduleIsSuccess() {
        val terminal = WaitPolicy43284.await(
            Recorder(),
            { _ -> },
            { path -> path == WaitPolicy43284.MODULE_PATH },
            4,
        )
        assertEquals(WaitPolicy43284.TERMINAL_SUCCESS, terminal)
    }

    @Test
    fun nothingSeenStaysInflightAfterTheBudget() {
        val recorder = Recorder()
        val lines = mutableListOf<String>()
        val terminal = WaitPolicy43284.await(recorder, { line -> lines += line }, { false }, 3)
        assertEquals(WaitPolicy43284.TERMINAL_INFLIGHT, terminal)
        assertEquals(3, recorder.calls)
        assertEquals(1, lines.size)
    }
}
