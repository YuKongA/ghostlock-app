package com.ghostlock.app.backend.cve43499.ktstep.run

import com.ghostlock.app.core.engine.StepCatalog
import com.ghostlock.app.core.engine.StepSpec
import com.ghostlock.app.core.steps.OpsInvoker
import com.ghostlock.app.core.steps.StepId
import com.ghostlock.app.core.steps.StepInputs
import com.ghostlock.app.core.steps.StepPlans

/** 43499 的步骤目录：只认 w1/w2/w3，顺序由调用方选择。 */
class Cve43499StepCatalog : StepCatalog<StepInputs> {
    override val backendId: String = BACKEND
    private val own = listOf(StepId.W1, StepId.W2, StepId.W3)

    override fun steps(): List<StepSpec> = own.map { spec(it) }

    override fun select(tokens: List<String>): Pair<List<StepSpec>, String?> {
        val selected = mutableListOf<StepSpec>()
        for (token in tokens) {
            val step = StepId.ofToken(token)
            if (step == null || step !in own) return emptyList<StepSpec>() to ("step-unknown:" + token)
            selected += spec(step)
        }
        return selected to null
    }

    override fun plan(spec: StepSpec, inputs: StepInputs): StepPlans.StepPlan {
        val step = StepId.ofToken(spec.token) ?: return StepPlans.StepPlan.Missing("step")
        return StepPlans.forStep(step, inputs)
    }

    fun nodes(selected: List<StepSpec>, inputs: StepInputs, ops: OpsInvoker): List<Cve43499OpNode> =
        selected.map { spec ->
            val step = StepId.ofToken(spec.token) ?: error("unknown selected step " + spec.token)
            Cve43499OpNode(spec.key, spec.token, step, inputs, ops)
        }

    private fun spec(step: StepId) = StepSpec(BACKEND, step.token, step.token)

    private companion object {
        const val BACKEND = "cve_2026_43499"
    }
}
