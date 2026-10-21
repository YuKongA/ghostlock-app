package com.ghostlock.app.backend.cve43499.ktstep.run

import com.ghostlock.app.core.engine.Overall
import com.ghostlock.app.core.engine.PipelineEngine
import com.ghostlock.app.core.engine.PipelineResult
import com.ghostlock.app.core.engine.RunNodeResult
import com.ghostlock.app.core.steps.OpsInvoker
import com.ghostlock.app.engine.ktstep.KtEnginePhase
import com.ghostlock.app.engine.ktstep.KtOpsInvoker
import com.ghostlock.app.engine.ktstep.KtStepPlan
import com.ghostlock.app.engine.ktstep.KtStepPlanValidator
import com.ghostlock.app.engine.ktstep.KernelRoute
import com.ghostlock.app.engine.ktstep.ProductionProbe
import com.ghostlock.app.engine.ktstep.ProductionRoute
import com.ghostlock.app.engine.ktstep.ProbeKind
import com.ghostlock.app.engine.ktstep.SyscallResult
import com.ghostlock.app.engine.ktstep.VictimProbe

data class Cve43499RunOutcome(
    val phase: KtEnginePhase,
    val reason: String?,
    val pipeline: PipelineResult?,
    val uid: Long? = null,
)

/** 43499 production-shaped run over PipelineEngine; not yet wired into KtStepEngine. */
class Cve43499Run(
    private val plan: KtStepPlan,
    private val route: KernelRoute,
    private val probe: VictimProbe,
    private val log: (String) -> Unit = {},
    private val ops: OpsInvoker = KtOpsInvoker(route, plan),
) {
    @Volatile private var activeEngine: PipelineEngine? = null

    /** Non-suspend cancel for BackendRun.stop(). */
    fun stop() { activeEngine?.abort("cancelled") }

    fun traces(): List<com.ghostlock.app.engine.ktstep.KtStepTrace> =
        (ops as? KtOpsInvoker)?.traces?.toList() ?: emptyList()
    suspend fun run(): Cve43499RunOutcome {
        val problem = KtStepPlanValidator.validate(plan)
        if (problem != null) return Cve43499RunOutcome(KtEnginePhase.Failed, problem, null)
        val catalog = Cve43499StepCatalog()
        val (selected, selectionError) = catalog.select(listOf("w1", "w2", "w3"))
        if (selectionError != null) {
            return Cve43499RunOutcome(KtEnginePhase.Failed, selectionError, null)
        }
        val engine = PipelineEngine(log)
        activeEngine = engine
        val pipeline = try {
            engine.run(catalog.nodes(selected, plan.stepInputs(), ops))
        } finally {
            engine.close()
            activeEngine = null
        }
        if (pipeline.overall != Overall.Ok) {
            return Cve43499RunOutcome(
                KtEnginePhase.Failed,
                reasonOf(pipeline.primary),
                pipeline,
            )
        }
        val uid = when (val answer = probe.uid()) {
            is SyscallResult.Ok -> answer.value
            is SyscallResult.Errno -> return Cve43499RunOutcome(
                KtEnginePhase.Failed, "probe:" + answer.code, pipeline,
            )
        }
        if (uid != plan.expectedUid) {
            return Cve43499RunOutcome(KtEnginePhase.Failed, "victim-uid:" + uid, pipeline, uid)
        }
        val production = route is ProductionRoute && probe is ProductionProbe
        return Cve43499RunOutcome(
            if (production) KtEnginePhase.Succeeded else KtEnginePhase.FixtureSucceeded,
            null, pipeline, uid,
        )
    }

    private fun reasonOf(primary: RunNodeResult?): String = when (primary) {
        is RunNodeResult.Refused -> primary.reason
        is RunNodeResult.Frozen -> primary.reason
        is RunNodeResult.Retryable -> primary.reason
        is RunNodeResult.Done, null -> "pipeline-failed"
    }
}
