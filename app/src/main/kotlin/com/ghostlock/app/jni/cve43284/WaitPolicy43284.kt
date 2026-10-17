package com.ghostlock.app.jni.cve43284

/**
 * wait 的统一口径与双判据（Kotlin 侧唯一的权威）。
 *
 * 参照 ghostlock-app-ref@5f184760：
 *  - cve_2026_43284_state.hpp:29-31：15000 / 40 / 5；
 *  - real_ops.cpp:400-420：**单循环**跑满预算，**先判失败**再判成功，主标记与备用标记**同一轮**都判，
 *    并把"模块存在"当作成功判据；/dev/df 只表示钩子起过火，不是终态。
 */
object WaitPolicy43284 {
    const val TIMEOUT_MS: Long = 15_000L
    const val POLL_MS: Long = 40L
    const val POLL_ATTEMPTS: Long = 5L
    const val ALT_BUDGET_MS: Long = POLL_MS * POLL_ATTEMPTS

    const val TERMINAL_FAILURE: Long = 0L
    const val TERMINAL_SUCCESS: Long = 1L
    const val TERMINAL_INFLIGHT: Long = 2L

    const val SUCCESS_MARKER: String = "/data/local/tmp/.ghostlock_lkm_ok"
    const val SUCCESS_MARKER_ALT: String = "/dev/dfm0"
    const val FAILURE_MARKER: String = "/data/local/tmp/.ghostlock_lkm_fail"
    const val FAILURE_MARKER_ALT: String = "/dev/dfm1"
    const val IN_FLIGHT_MARKER: String = "/dev/df"
    const val MODULE_PATH: String = "/sys/module/dirtyfrag"
    const val MODULE_NAME: String = "kernelsu"

    fun interface TerminalProbe {
        fun await(budgetMs: Long, pollMs: Long, successMarker: String, failureMarker: String): Long
    }

    fun terminalName(state: Long): String = when (state) {
        TERMINAL_FAILURE -> "failure"
        TERMINAL_SUCCESS -> "success"
        TERMINAL_INFLIGHT -> "inflight"
        else -> "unknown(" + state + ")"
    }

    /**
     * 单循环终态等待，判据顺序照 ref real_ops.cpp:400-420。
     *
     * @param probe 调用缝
     * @param log 逐行日志
     * @param exists 存在性判据（模块目录用；单测注入假实现）
     * @param maxTicks 最多轮数（默认按 15000 / (40×2) 折算；单测可给小值）
     * @return 终态
     */
    fun await(
        probe: TerminalProbe,
        log: (String) -> Unit = {},
        exists: (String) -> Boolean = { path -> java.io.File(path).exists() },
        maxTicks: Int = (TIMEOUT_MS / (POLL_MS * 2L)).toInt(),
    ): Long {
        var ticks = 0
        while (ticks < maxTicks) {
            ticks += 1
            val primary = probe.await(POLL_MS, POLL_MS, SUCCESS_MARKER, FAILURE_MARKER)
            if (primary == TERMINAL_FAILURE) {
                log("wait: failure (primary) tick=" + ticks + " marker=" + FAILURE_MARKER)
                return TERMINAL_FAILURE
            }
            val alternate = probe.await(POLL_MS, POLL_MS, SUCCESS_MARKER_ALT, FAILURE_MARKER_ALT)
            if (alternate == TERMINAL_FAILURE) {
                log("wait: failure (alternate) tick=" + ticks + " marker=" + FAILURE_MARKER_ALT)
                return TERMINAL_FAILURE
            }
            if (primary == TERMINAL_SUCCESS) {
                log("wait: success (primary) tick=" + ticks + " marker=" + SUCCESS_MARKER)
                return TERMINAL_SUCCESS
            }
            if (alternate == TERMINAL_SUCCESS) {
                log("wait: success (alternate) tick=" + ticks + " marker=" + SUCCESS_MARKER_ALT)
                return TERMINAL_SUCCESS
            }
            if (exists(MODULE_PATH)) {
                log("wait: success (module present) tick=" + ticks + " path=" + MODULE_PATH)
                return TERMINAL_SUCCESS
            }
        }
        log("wait: inflight after " + ticks + " ticks")
        return TERMINAL_INFLIGHT
    }
}
