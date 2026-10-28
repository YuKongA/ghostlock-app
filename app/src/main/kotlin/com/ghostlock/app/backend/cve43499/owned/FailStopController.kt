package com.ghostlock.app.backend.cve43499.owned

/**
 * G01 dirty/retained fail-stop 控制器（P1b-1；legacy `exit_group(70)` 同形）。
 *
 * 语义：
 * - 只对 **DIRTY/retained** 结果调用；clean/普通失败路径不得触发；
 * - **先**落 evidence/日志（[evidenceSink] + [logger]，调用方负责 flush），**再**调用
 *   [terminator] 强制终止（默认 `Process.killProcess(myPid())`）；
 * - **只触发一次**（并发/重入安全）；`enabled=false` 时完全 no-op（仅记录不终止）；/
 * - reason 冻结格式 `fail-stop:<stage>:<retainedReason>`。
 *
 * @param enabled debug 配置 `failStop.enabled`（1=终止；0=只记录）。
 * @param terminator 终止口（生产=android.os.Process.killProcess(myPid())；单测=注入 fake，
 *   不得真杀）。
 * @param evidenceSink evidence 落盘/合并口（必须在 [terminator] 之前调用；生产负责 flush）。
 * @param logger 日志落盘口（必须在 [terminator] 之前调用；生产写 logcat/文件）。
 * @note 无全局状态；一个 controller 属于一次 run（Flow 持有）。
 * @note 阶段：P1b-1（task-64）；真机行为 UNVERIFIED（设备离线）。
 */
internal class FailStopController(
    private val enabled: Boolean,
    private val terminator: (reason: String) -> Unit = DEFAULT_TERMINATOR,
    private val evidenceSink: (Map<String, String>) -> Unit = {},
    private val logger: (line: String) -> Unit = {},
) {
    private val lock: Any = Any()
    private var fired: Boolean = false

    /**
     * 触发一次 fail-stop（先 evidence/日志，再终止；最多一次）。
     *
     * @param stage 阶段（稳定 token，例如 `write.w1`/`lifecycle.close-chain`）。
     * @param retainedReason 触发 retained 的具名原因（调用方 evidence reason）。
     * @param retained 触发时是否 retained（当前恒 true；保留参数便于 caller 明确）。
     * @return true=本次调用真正触发；false=已触发过/未启用。
     */
    fun trigger(stage: String, retainedReason: String, retained: Boolean = true): Boolean {
        synchronized(lock) {
            if (!enabled || fired) return false
            fired = true
        }
        val reason = "fail-stop:" + stage + ":" + retainedReason
        val evidence = linkedMapOf(
            "failStopTriggered" to "true",
            "failStopReason" to reason,
            "failStopStage" to stage,
            "failStopPrecededByRetained" to retained.toString(),
        )
        /* 顺序冻结：evidence→logger→terminate；前两步任何异常都不阻止终止。 */
        runCatching { evidenceSink(evidence) }
        runCatching { logger(reason + " retained=" + retained) }
        runCatching { terminator(reason) }
        return true
    }

    /** @return 是否已经触发过 fail-stop。 */
    fun isTriggered(): Boolean = synchronized(lock) { fired }

    /** @return 是否启用（false=只记录不终止）。 */
    fun isEnabled(): Boolean = enabled

    companion object {
        /**
         * 生产默认终止口：`Process.killProcess(Process.myPid())`（legacy exit_group 同义）。
         *
         * @note 仅在真机/Android 进程内调用；宿主单测必须注入 fake，绝不触达本 lambda。
         */
        val DEFAULT_TERMINATOR: (String) -> Unit = {
            android.os.Process.killProcess(android.os.Process.myPid())
        }
    }
}
