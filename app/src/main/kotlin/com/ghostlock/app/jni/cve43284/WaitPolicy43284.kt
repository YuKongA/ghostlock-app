package com.ghostlock.app.jni.cve43284

/**
 * wait 的统一口径。
 *
 * 不再使用固定 marker；成功只认实际效果：
 *  - /system/bin/su 或 /system/xbin/su 存在
 *  - /sys/module/kernelsu 或 /sys/module/dirtyfrag 存在
 *
 * 失败由上层退出码决定；本类不写、不读任何自定义 marker。
 */
object WaitPolicy43284 {
    const val TIMEOUT_MS: Long = 15_000L
    const val POLL_MS: Long = 40L
    const val POLL_ATTEMPTS: Long = 5L
    const val ALT_BUDGET_MS: Long = POLL_MS * POLL_ATTEMPTS

    const val TERMINAL_FAILURE: Long = 0L
    const val TERMINAL_SUCCESS: Long = 1L
    const val TERMINAL_INFLIGHT: Long = 2L

    /** 实际效果，不是 marker。 */
    const val SUCCESS_PATH: String = "/system/bin/su"
    const val SUCCESS_PATH_ALT: String = "/system/xbin/su"
    const val MODULE_PATH: String = "/sys/module/kernelsu"
    const val MODULE_PATH_ALT: String = "/sys/module/dirtyfrag"
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

    fun await(
        probe: TerminalProbe,
        log: (String) -> Unit = {},
        exists: (String) -> Boolean = { path -> java.io.File(path).exists() },
        maxTicks: Int = (TIMEOUT_MS / (POLL_MS * 2L)).toInt(),
    ): Long {
        var ticks = 0
        while (ticks < maxTicks) {
            ticks += 1
            if (exists(SUCCESS_PATH) || exists(SUCCESS_PATH_ALT)) {
                log("wait: success (su) tick=" + ticks)
                return TERMINAL_SUCCESS
            }
            if (exists(MODULE_PATH) || exists(MODULE_PATH_ALT)) {
                log("wait: success (module) tick=" + ticks)
                return TERMINAL_SUCCESS
            }
            probe.await(POLL_MS, POLL_MS, "", "")
        }
        log("wait: inflight after " + ticks + " ticks")
        return TERMINAL_INFLIGHT
    }
}
