package com.ghostlock.app.jni.cve43284

/**
 * wait 的统一口径与双判据（Kotlin 侧唯一的权威）。
 *
 * 参照 `ghostlock-app-ref@5f184760`：
 *  - `src/core/backend/cve_2026_43284_state.hpp:29-31`：`15000 / 40 / 5`（wait_timeout_ms /
 *    module_poll_attempts / module_poll_interval_ms）是这三项的唯一定义处；
 *  - `src/core/backend/cve_2026_43284/real_ops.cpp:408-414`：wait 同时接受 **shell 可读的备用标记**
 *    （`real_ops.hpp:59-74` 的 kLkmSuccessMarkerAlt/kLkmFailureMarkerAlt）——Enforcing 下 shell/app 域
 *    stat `/dev/dfm0` 会被拒，只盯它必然假超时；
 *  - 同文件 `:405-415`：**先失败后成功**（陈旧的成功标记不许掩盖新的失败标记）。
 *
 * 本批的统一口径（用户裁定，四项之一）：
 *  - 预算 [TIMEOUT_MS] = 15000 ms —— 与 ref 同一数字；
 *  - 轮询 [POLL_MS] = 40 ms、备用判据 [POLL_ATTEMPTS] = 5 次 —— 与 ref 的 `40 / 5` 同一三元组
 *    （ref 原意是模块握手 40 次 × 5 ms，这里用作 wait 的节拍与备用次数，数值不变、位置统一）。
 *
 * 判据（任一命中即算）：
 *  - 成功：`/data/local/tmp/.ghostlock_lkm_ok`（优先，可读）或 `/dev/dfm0`（第二判据）；
 *  - 失败：`/data/local/tmp/.ghostlock_lkm_fail` 或 `/dev/dfm1`。
 *
 * 为什么分两段：native 的 `waitTerminal` 只带一条成功标记 + 一条失败标记（`Wait43284`），
 * 而 ref 的循环每一轮同时 stat 四个路径。这里先按优先判据跑满预算，仍在飞时再用一小段备用窗口
 * 判第二组标记 —— 结果仍然是「任一命中即成功」，且不改变 native 原语。
 */
object WaitPolicy43284 {
    /** 终态等待预算（ref state.hpp:29 = 15000）。 */
    const val TIMEOUT_MS: Long = 15_000L

    /** 轮询节拍（ref state.hpp:30-31 的 40 / 5 三元组之一）。 */
    const val POLL_MS: Long = 40L

    /** 备用判据的轮询次数（ref 三元组的另一半；每轮一条 stat）。 */
    const val POLL_ATTEMPTS: Long = 5L

    /** 备用判据的窗口：5 次 × 40 ms。 */
    const val ALT_BUDGET_MS: Long = POLL_MS * POLL_ATTEMPTS

    /** 终态（与 core 的 `TerminalState` 同值：0 failure / 1 success / 2 inflight）。 */
    const val TERMINAL_FAILURE: Long = 0L
    const val TERMINAL_SUCCESS: Long = 1L
    const val TERMINAL_INFLIGHT: Long = 2L

    /** 成功标记（优先）：UMH 脚本在 /data/local/tmp 下 touch，任何域都读得到（ref real_ops.hpp:67）。 */
    const val SUCCESS_MARKER: String = "/data/local/tmp/.ghostlock_lkm_ok"

    /** 成功标记（第二判据）：LKM 以 root 建（ref real_ops.hpp:59）。 */
    const val SUCCESS_MARKER_ALT: String = "/dev/dfm0"

    /** 失败标记（ref real_ops.hpp:68；/dev/dfm1 见 :60）。 */
    const val FAILURE_MARKER: String = "/data/local/tmp/.ghostlock_lkm_fail"

    /** 失败标记（第二判据，ref real_ops.hpp:60）。 */
    const val FAILURE_MARKER_ALT: String = "/dev/dfm1"

    /** 载荷互斥标记「正在加载」，**不是**终态：看到它只说明钩子起过火（ref real_ops.hpp:69-71）。 */
    const val IN_FLIGHT_MARKER: String = "/dev/df"

    /** 模块目录：出现过又消失 = 模块跑完 UMH 自卸载（ref real_ops.hpp:72-74）。 */
    const val MODULE_PATH: String = "/sys/module/dirtyfrag"

    /** 模块名（native 侧的模块标记串）。 */
    const val MODULE_NAME: String = "kernelsu"

    /** 一次终态等待的调用缝：设备上是 [Assembled43284.waitTerminal]，单测里是假的。 */
    fun interface TerminalProbe {
        /**
         * @param budgetMs 预算（ms）
         * @param pollMs   轮询节拍（ms）
         * @param successMarker 成功标记路径
         * @param failureMarker 失败标记路径
         * @return [TERMINAL_FAILURE] / [TERMINAL_SUCCESS] / [TERMINAL_INFLIGHT]
         */
        fun await(budgetMs: Long, pollMs: Long, successMarker: String, failureMarker: String): Long
    }

    /** 终态的具名形态（只用于日志）。 */
    fun terminalName(state: Long): String = when (state) {
        TERMINAL_FAILURE -> "failure"
        TERMINAL_SUCCESS -> "success"
        TERMINAL_INFLIGHT -> "inflight"
        else -> "unknown(" + state + ")"
    }

    /**
     * 两段式终态等待：优先判据跑满 [TIMEOUT_MS]，仍在飞再给备用判据 [ALT_BUDGET_MS]。
     *
     * @param probe 调用缝（见 [TerminalProbe]）
     * @param log   逐行日志回调
     * @return 终态：成功/失败/仍在飞（调用缝本身失败时为 inflight，由上层如实记）
     */
    fun await(probe: TerminalProbe, log: (String) -> Unit = {}): Long {
        val primary = probe.await(TIMEOUT_MS, POLL_MS, SUCCESS_MARKER, FAILURE_MARKER)
        log(
            "wait: primary=" + terminalName(primary) + " budget=" + TIMEOUT_MS + "ms poll=" + POLL_MS +
                "ms markers=" + SUCCESS_MARKER + "|" + FAILURE_MARKER,
        )
        if (primary != TERMINAL_INFLIGHT) {
            return primary
        }
        /* 主判据没命中不等于没发生：/dev/dfm0|dfm1 是第二判据（ref real_ops.cpp:408-414 的两条 alt 路径）。 */
        val alternate = probe.await(ALT_BUDGET_MS, POLL_MS, SUCCESS_MARKER_ALT, FAILURE_MARKER_ALT)
        log(
            "wait: alternate=" + terminalName(alternate) + " budget=" + ALT_BUDGET_MS + "ms poll=" + POLL_MS +
                "ms markers=" + SUCCESS_MARKER_ALT + "|" + FAILURE_MARKER_ALT,
        )
        return alternate
    }
}
