package com.ghostlock.app.core.steps

/**
 * 步骤引擎：按声明顺序跑，缺前置不动手，拒绝即停，不确定则冻结。
 *
 * 它自己不做任何设备动作：每个步骤通过 [OpsInvoker] 发一笔原生事务，其余（顺序、重试次数、
 * 停止规则、结果归档）都在这里 —— 也就是"上层逻辑留在 Kotlin"的那部分。
 *
 * 四条规则，逐条对应计划 §4.5：
 *  1. 严格按顺序；某步拒绝 ⇒ 立即停止（后面的步骤不会被执行）。
 *  2. 前置缺失 ⇒ 该步拒绝，且**不发出任何原生调用**（precondition:<原因>）。
 *  3. 结果不确定（native 答 indeterminate）⇒ 冻结：此后不再发出任何调用，且不允许重试。
 *  4. 重试只按 [StepInputs.attempts] 来；重试只针对"没准备好"这类可重来的拒绝。
 */
class StepEngine(
    private val ops: OpsInvoker,
    private val log: (String) -> Unit = {},
) {
    /**
     * 跑一遍。
     *
     * @return 每一步的结果，按执行顺序；被停下的步骤也会有一条（原因写在里面）。
     */
    fun run(plan: List<StepId>, inputs: StepInputs): List<StepResult> {
        val results = mutableListOf<StepResult>()
        for (step in plan) {
            if (frozen) {
                results += StepResult(step, StepOutcome.Frozen, "engine-frozen")
                continue
            }
            val outcome = runStep(step, inputs)
            results += outcome
            if (outcome.outcome == StepOutcome.Refused) {
                /* 拒绝即停：继续跑后面的步骤，等于在一个已知不成立的前提上继续动手。 */
                break
            }
            /* 冻结不 break：剩下的步骤各留一条「被冻结」的结果，界面上才看得出计划断在哪，
             * 而不是像没写进去一样凭空少了几步。 */
        }
        return results
    }

    /** 出现不确定效果后置位：引擎不许再动，也不许重试。 */
    private var frozen = false

    private fun runStep(step: StepId, inputs: StepInputs): StepResult {
        val calls = mutableListOf<String>()
        val plan = when (val planned = StepPlans.forStep(step, inputs)) {
            is StepPlans.StepPlan.Missing -> return StepResult(
                step,
                StepOutcome.Refused,
                "precondition:" + step.token + "-" + planned.input,
            )

            is StepPlans.StepPlan.Ready -> planned.calls
        }
        var attempt = 1
        while (true) {
            var last: OpsReply? = null
            for (call in plan) {
                calls += call.opName
                val reply = ops.invoke(call.opName, call.values, call.blobs)
                last = reply
                log(
                    "step=" + step.token + " call=" + call.opName + " status=" + reply.status +
                        (reply.refusal?.let { " refusal=" + it } ?: ""),
                )
                if (reply.status == "indeterminate") {
                    /* 效果不确定：冻结，且这一条理由必须原样带出去。 */
                    frozen = true
                    return StepResult(step, StepOutcome.Frozen, reply.refusal ?: "indeterminate", calls)
                }
                if (!reply.ok) {
                    val retryable = reply.status == "not-ready" && attempt < inputs.attempts
                    if (retryable) {
                        attempt += 1
                        if (inputs.settleMs > 0L) {
                            Thread.sleep(inputs.settleMs)
                        }
                        break
                    }
                    return StepResult(
                        step,
                        StepOutcome.Refused,
                        reply.refusal ?: reply.status,
                        calls,
                    )
                }
            }
            if (last?.ok == true) {
                return StepResult(step, StepOutcome.Ran, null, calls)
            }
            if (attempt > inputs.attempts) {
                return StepResult(step, StepOutcome.Refused, last?.refusal ?: "attempts-exhausted", calls)
            }
            if (last == null) {
                return StepResult(step, StepOutcome.Refused, "precondition:" + step.token + "-no-calls", calls)
            }
            if (last.status != "not-ready") {
                return StepResult(step, StepOutcome.Refused, last.refusal ?: last.status, calls)
            }
        }
    }
}
