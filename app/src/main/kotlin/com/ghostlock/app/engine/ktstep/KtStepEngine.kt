package com.ghostlock.app.engine.ktstep

import com.ghostlock.app.core.steps.StepId
import com.ghostlock.app.core.steps.StepOutcome
import com.ghostlock.app.core.steps.StepResult

enum class KtEnginePhase { Succeeded, FixtureSucceeded, Failed }

data class KtStepOutcome(
    val phase: KtEnginePhase,
    val reason: String?,
    val steps: List<StepResult>,
    val traces: List<KtStepTrace>,
    val uid: Long? = null,
    val fixtureSucceeded: Boolean = false,
) {
    val succeeded: Boolean get() = phase == KtEnginePhase.Succeeded
}

/**
 * kt-step 的 Kotlin 引擎骨架：PipelineEngine 决定顺序/重试/冻结，KtOpsInvoker 负责布局/页/路由。
 *
 * 它不选 route、不读 profile、不碰 UI；路由实现由调用方注入。
 */
class KtStepEngine(
    private val route: KernelRoute,
    private val probe: VictimProbe,
    private val log: (String) -> Unit = {},
) {
    fun run(plan: KtStepPlan): KtStepOutcome = kotlinx.coroutines.runBlocking { runSuspend(plan) }

    suspend fun runSuspend(plan: KtStepPlan): KtStepOutcome {
        val problem = KtStepPlanValidator.validate(plan)
        if (problem != null) return KtStepOutcome(KtEnginePhase.Failed, problem, emptyList(), emptyList())
        val invoker = KtOpsInvoker(route, plan)
        val backend = com.ghostlock.app.backend.cve43499.ktstep.run.Cve43499BackendRun(
            plan, route, probe = probe, ops = invoker,
        )
        val engine = com.ghostlock.app.core.engine.DelegatingExecutionEngine(backend, log)
        val result = try {
            engine.execute(
                com.ghostlock.app.core.engine.BackendRunRequest(
                    backendId = backend.backendId,
                    pathId = BACKEND,
                    values = emptyMap(),
                ),
            )
        } finally {
            engine.close()
        }
        val steps = result.steps.mapNotNull { node -> toStepResult(node) }
        return when (result) {
            is com.ghostlock.app.core.engine.BackendRunResult.Failed -> KtStepOutcome(
                KtEnginePhase.Failed, result.reason, steps, invoker.traces.toList(), result.uid,
            )
            is com.ghostlock.app.core.engine.BackendRunResult.Ok -> {
                val phase = if (result.note == KtEnginePhase.Succeeded.name) {
                    KtEnginePhase.Succeeded
                } else {
                    KtEnginePhase.FixtureSucceeded
                }
                KtStepOutcome(
                    phase = phase,
                    reason = null,
                    steps = steps,
                    traces = invoker.traces.toList(),
                    uid = result.uid,
                    fixtureSucceeded = phase == KtEnginePhase.FixtureSucceeded,
                )
            }
        }
    }

    private fun toStepResult(node: com.ghostlock.app.core.engine.NodeOutcome): StepResult? {
        val step = StepId.ofToken(node.node.token) ?: return null
        val mapped = when (val result = node.result) {
            is com.ghostlock.app.core.engine.RunNodeResult.Done -> StepOutcome.Ran
            is com.ghostlock.app.core.engine.RunNodeResult.Refused -> StepOutcome.Refused
            is com.ghostlock.app.core.engine.RunNodeResult.Frozen -> StepOutcome.Frozen
            is com.ghostlock.app.core.engine.RunNodeResult.Retryable -> StepOutcome.Refused
        }
        val reason = when (val result = node.result) {
            is com.ghostlock.app.core.engine.RunNodeResult.Done -> null
            is com.ghostlock.app.core.engine.RunNodeResult.Refused -> result.reason
            is com.ghostlock.app.core.engine.RunNodeResult.Frozen -> result.reason
            is com.ghostlock.app.core.engine.RunNodeResult.Retryable -> result.reason
        }
        return StepResult(step, mapped, reason, node.calls)
    }

    private companion object {
        const val BACKEND = "cve_2026_43499"
    }
}
