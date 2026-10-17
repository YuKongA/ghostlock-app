package com.ghostlock.app.jni.cve43284

/**
 * 会话间序号的自动接续（Kotlin 侧的 `ctx->page.next_seq`）。
 *
 * 参照 `ghostlock-app-ref@5f184760` 的三处「自动交棒」：
 *  - `src/core/backend/cve_2026_43284/real_ops.cpp:250-257`：patch#1 的写窗口先
 *    `crash.next_seq = ctx->page.next_seq`，写完 `ctx->page.next_seq = crash.next_seq` 交回；
 *  - 同文件 `:307-327`：hook 窗口 `ctx->hook_page.next_seq = ctx->page.next_seq`，apply 完
 *    `ctx->page.next_seq = ctx->hook_page.next_seq`；
 *  - 同文件 `:339-344`：restore 走同一条 SA/套接字，序号同样连续。
 *
 * 同一条 SA 上的所有窗口共用**一个**计数器 —— 重置会被内核防重放窗口丢包（本机真机证据：两条链各自从 1 起，
 * 其中一条整条不落地）。本类只持有这一个计数器：链首由首次初始化给出，之后每次写成功都由 native 返回的序号推进。
 *
 * 用法（见 `Assembled43284.writeImage` / `writeHook` / `restoreHook`）：
 *  1. 建实例 `SequenceContinuation(::step)`（一次运行一个，链内共享；**不是**全局状态）；
 *  2. 每个会话的 `initializeAccessAndRemember(params, saInit(seq.value))` 取当前值；
 *  3. 每次写成功后 `advance(native 返回的序号)`，日志行固定为 `seq: <prev> -> <next> (auto)`。
 *
 * @param log 逐行日志回调（与探针的 step 同一条去处：屏幕 / logcat / 文件）。
 * @param initial 链首的初始序号（0 = 还没有值，首次初始化按 native 默认处理）。
 */
class SequenceContinuation(
    private val log: (String) -> Unit,
    initial: Long = 0L,
) {
    /** 当前应交给下一次初始化/写的序号（0 = 还没有值）。 */
    var value: Long = initial
        private set

    /**
     * 用 native 返回的序号推进（ref 的「交回」那半步）。
     *
     * @param returned `nextSequence()` 读回的窗口序号；`<= 0` 表示这次查询没拿到值。
     * @return 推进后的序号；查询失败时保持原值（把计数器按回 0 正是防重放丢包的形态）。
     * @note 日志两种形态：`seq: <prev> -> <next> (auto)` 与
     *       `seq: <prev> -> <prev> (auto) query-refused=<returned>`（只记事实，不猜）。
     */
    fun advance(returned: Long): Long {
        val previous = value
        if (returned <= 0L) {
            log("seq: " + previous + " -> " + previous + " (auto) query-refused=" + returned)
            return value
        }
        value = returned
        log("seq: " + previous + " -> " + returned + " (auto)")
        return value
    }
}
