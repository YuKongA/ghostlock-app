package com.ghostlock.app.backend.cve43499.ktstep.run

import com.ghostlock.app.core.engine.BackendRun
import com.ghostlock.app.core.engine.BackendRunFactory
import com.ghostlock.app.core.engine.BackendRunRequest
import com.ghostlock.app.core.engine.BackendRunResult
import com.ghostlock.app.core.engine.Progress
import com.ghostlock.app.core.steps.OpsInvoker
import com.ghostlock.app.engine.ktstep.KernelRoute
import com.ghostlock.app.engine.ktstep.KtOpsInvoker
import com.ghostlock.app.engine.ktstep.KtEnginePhase
import com.ghostlock.app.engine.ktstep.KtStepPlan
import com.ghostlock.app.engine.ktstep.VictimProbe

/** Wraps Cve43499Run as the one-shot BackendRun boundary. */
class Cve43499BackendRun(
    private val plan: KtStepPlan,
    private val route: KernelRoute,
    override val backendId: String = BACKEND,
    private val probe: VictimProbe,
    private val ops: OpsInvoker? = null,
) : BackendRun {
    @Volatile private var active: Cve43499Run? = null

    override suspend fun run(request: BackendRunRequest, progress: Progress): BackendRunResult {
        if (request.backendId != backendId) return BackendRunResult.Failed("backend-mismatch", emptyList())
        val run = Cve43499Run(plan, route, probe, ops = ops ?: KtOpsInvoker(route, plan))
        active = run
        val outcome = try { run.run() } finally { active = null }
        val steps = outcome.pipeline?.outcomes.orEmpty()
        steps.forEachIndexed { index, node -> progress.onStep(node.node.token, index, steps.size) }
        return when (outcome.phase) {
            KtEnginePhase.Succeeded, KtEnginePhase.FixtureSucceeded ->
                BackendRunResult.Ok(outcome.phase.name, outcome.uid, steps)
            KtEnginePhase.Failed ->
                BackendRunResult.Failed(outcome.reason ?: "43499-failed", steps, outcome.uid)
        }
    }

    override fun stop() { active?.stop() }
    override fun close() { stop() }
    override suspend fun shutdown() = Unit

    private companion object { const val BACKEND = "cve_2026_43499" }
}

class Cve43499RunFactory(
    private val plan: KtStepPlan,
    private val route: KernelRoute,
    private val probe: VictimProbe,
) : BackendRunFactory {
    override val backendId: String = "cve_2026_43499"
    override fun create(): BackendRun = Cve43499BackendRun(plan, route, backendId, probe)
}
