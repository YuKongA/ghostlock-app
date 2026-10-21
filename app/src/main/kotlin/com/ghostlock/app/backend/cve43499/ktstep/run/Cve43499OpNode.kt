package com.ghostlock.app.backend.cve43499.ktstep.run

import com.ghostlock.app.core.engine.RunNode
import com.ghostlock.app.core.engine.RunNodeResult
import com.ghostlock.app.core.engine.RunPolicy
import com.ghostlock.app.core.engine.SkipPolicy
import com.ghostlock.app.core.steps.OpsInvoker
import com.ghostlock.app.core.steps.StepId
import com.ghostlock.app.core.steps.StepInputs
import com.ghostlock.app.core.steps.StepPlans

/** 43499 的单步适配：StepPlans -> OpsInvoker -> RunNodeResult；不含顺序/重试策略。 */
class Cve43499OpNode(
    override val key: String,
    override val token: String,
    private val step: StepId,
    private val inputs: StepInputs,
    private val ops: OpsInvoker,
) : RunNode {
    override val backendId: String = BACKEND
    override val policy: RunPolicy = RunPolicy(
        attempts = inputs.attempts,
        settleMs = inputs.settleMs,
        skipPolicy = SkipPolicy.FrozenRemaining,
    )

    private val calls = mutableListOf<String>()

    override suspend fun run(): RunNodeResult {
        val plan = when (val planned = StepPlans.forStep(step, inputs)) {
            is StepPlans.StepPlan.Missing ->
                return RunNodeResult.Refused("precondition:" + step.token + "-" + planned.input)
            is StepPlans.StepPlan.Ready -> planned.calls
        }
        if (plan.isEmpty()) {
            return RunNodeResult.Refused("precondition:" + step.token + "-no-calls")
        }
        for (call in plan) {
            calls += call.opName
            val reply = ops.invoke(call.opName, call.values, call.blobs)
            if (reply.status == "indeterminate") {
                return RunNodeResult.Frozen(reply.refusal ?: "indeterminate")
            }
            if (!reply.ok) {
                if (reply.status == "not-ready") {
                    return RunNodeResult.Retryable(reply.refusal ?: reply.status)
                }
                return RunNodeResult.Refused(reply.refusal ?: reply.status)
            }
        }
        return RunNodeResult.Done()
    }

    override fun calls(): List<String> = calls.toList()
    override fun abort(reason: String) = Unit
    override fun close() = Unit

    private companion object {
        const val BACKEND = "cve_2026_43499"
    }
}
