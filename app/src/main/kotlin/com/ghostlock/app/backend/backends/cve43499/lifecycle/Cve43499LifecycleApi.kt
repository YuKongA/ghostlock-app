package com.ghostlock.app.backend.backends.cve43499.lifecycle

import com.ghostlock.app.backend.backends.cve43499.owned.ActorPreparation
import com.ghostlock.app.backend.backends.cve43499.owned.Cve43499PrebuiltDiscardReceipt
import com.ghostlock.app.backend.backends.cve43499.owned.LandingConfig
import com.ghostlock.app.backend.backends.cve43499.owned.PlacementConfig
import com.ghostlock.app.backend.backends.cve43499.victim.VictimFinishResult

/**
 * 43499 唯一生命周期层的对外接口（R3d）。
 *
 * 提取接口的目的只有一个：Flow 单测可在**不触 Native**的前提下注入 fake 生命周期层
 * （fake lifecycle 直接返回 typed 结局与物理事实），从而覆盖多写链的步骤顺序、
 * 重试次数、fast-repair 与回执证据。生产实现是 [Cve43499Lifecycle]。
 *
 * @note 阶段：R3d；接口不新增任何编排语义（顺序仍由 Flow 独占）。
 */
internal interface Cve43499LifecycleApi {
    /**
     * 启动 victim 会话（幂等）。
     *
     * @return null 表示成功；否则为具名失败原因。
     */
    fun startVictim(): String?

    /**
     * 执行 Placement 定位事务并接管 owner（新开一条写链）。
     *
     * @param config Placement 物理参数。
     * @return typed 步骤结果（Prepared 携带 facts）。
     */
    fun preparePlacement(config: PlacementConfig): Cve43499PlacementStep

    /**
     * 消费最后一条链的 Placement 并落页。
     *
     * @param config landing 物理参数与最终字节。
     * @return typed 步骤结果。
     */
    fun landPage(config: LandingConfig): Cve43499LandingStep

    /**
     * 消费最后一条链的 Page 并准备 Actor。
     *
     * @param preparation Actor 调度与 Carrier 参数。
     * @return typed 步骤结果。
     */
    fun prepareActor(preparation: ActorPreparation): Cve43499ActorPrepareStep

    /**
     * 打开唯一一次 PI 窗口并执行最后一条链的 Actor。
     *
     * @return typed 步骤结果（携带物理事实）。
     */
    fun executeActor(): Cve43499ActorExecuteStep

    /**
     * 终结最后一条写链（成功/失败路径都可调用）。
     *
     * @return 单链 close 回执。
     */
    fun closeChain(): Cve43499ChainCloseReceipt

    /**
     * 终结全部写链与 victim。
     *
     * @return 聚合终结回执。
     */
    fun closeAll(): Cve43499LifecycleReceipt

    /* ==================================================================== */
    /* G05 prebuilt repair（P1b-2；Native 零改动/零 ABI，复用既有 JNI 入口）   */
    /* ==================================================================== */

    /**
     * 预建 repair 链的 placement（不进入 active chains）。
     *
     * @param config Placement 物理参数。
     * @return Prepared（携带 facts）/ 具名 Failed（已存在 prebuilt 或 Native 拒绝）。
     * @note 默认实现返回 unsupported；生产 [Cve43499Lifecycle] 覆盖。
     */
    fun prebuildRepairPlacement(config: PlacementConfig): Cve43499PlacementStep =
        Cve43499PlacementStep.Failed("prebuild:unsupported")

    /**
     * 预建 repair 链的 landing（PAGE_READY）。
     *
     * @param config landing 参数与最终字节。
     * @return typed landing 结局。
     */
    fun landPrebuildRepair(config: LandingConfig): Cve43499LandingStep =
        Cve43499LandingStep.Failed("prebuild:unsupported")

    /**
     * 把 PAGE_READY 的 prebuilt 页激活到 active chain 尾部（供最小事务 prepareActor）。
     *
     * @return Activated（含创建时间）/ Failed（状态非法/close-retained）。
     */
    fun activatePrebuiltRepair(): Cve43499PrebuiltActivationStep =
        Cve43499PrebuiltActivationStep.Failed(
            reason = "prebuild:unsupported",
            retained = false,
        )

    /**
     * clean/dirty discard prebuilt 链（B2 判定依据）。
     *
     * @return discard 回执（clean/retained/termination）。
     */
    fun discardPrebuiltRepair(): Cve43499PrebuiltDiscardReceipt =
        Cve43499PrebuiltDiscardReceipt(
            clean = true,
            retained = false,
            termination = "prebuilt=absent;",
        )

    /** @return true=PAGE_READY（可 activation 的最小事务）。 */
    fun prebuiltRepairAvailable(): Boolean = false

    /* G04/A2 append-only：W3 self-probe（不支持 → null/false ⇒ 该路径 FAIL）。 */
    fun victimStatus(): ByteArray? = null
    fun victimCommSet(marker: String): Boolean = false
    fun victimComm(): Pair<Int, ByteArray>? = null

    /** task-113 debug-only：最后 active chain 的 actor carrier 快照（int64[10] 或 null）。 */
    fun actorDebugSnapshot(): LongArray? = null

    /** @return victim 身份快照；未启动或读取失败返回 null。 */
    fun victimSnapshot(): Cve43499VictimSnapshot?

    /** @return victim uid 探测结果。 */
    fun victimUid(): Cve43499UidProbe

    /**
     * @return true=曾观察到 victim uid==0（W2 成功后置位）。
     * @note 默认 false：未覆写的 fake 走既有单 W1/App 路径；生产实现由 session 置位。
     */
    fun victimRooted(): Boolean = false

    /**
     * 在 rooted victim 内执行 finish 脚本（R3f/task-45）。
     *
     * @param scriptPath handoff 冻结的脚本绝对路径。
     * @param timeoutMs 有界等待（毫秒；必须 >0）。
     * @return 退出码+输出摘要或具名失败；实现不抛异常。
     * @note 默认实现返回 `victim-finish:unsupported`（fake 可覆写）。
     */
    fun victimFinish(scriptPath: String, timeoutMs: Long): VictimFinishResult =
        VictimFinishResult.Failed("victim-finish:unsupported")
}

/**
 * G05 prebuilt repair 激活结局（Lifecycle → Flow）。
 *
 * @property Activated.prebuiltNanos 预建创建时间（elapsedRealtimeNanos）。
 * @property Failed.reason 具名原因。
 * @property Failed.retained true=activation/close 存在 retained（W2 后按 close-retained 合同）。
 */
internal sealed interface Cve43499PrebuiltActivationStep {
    /** 页已转移到 active chain 尾部；可执行最小事务。 */
    data class Activated(val prebuiltNanos: Long) : Cve43499PrebuiltActivationStep

    /** 无法 activation（状态非法/retained）。 */
    data class Failed(val reason: String, val retained: Boolean) : Cve43499PrebuiltActivationStep
}
