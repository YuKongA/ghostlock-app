package com.ghostlock.app.backend.cve43499.ktstep.run

import com.ghostlock.app.engine.ktstep.KernelRoute
import com.ghostlock.app.engine.ktstep.KtStepEngine
import com.ghostlock.app.engine.ktstep.KtStepOutcome
import com.ghostlock.app.engine.ktstep.KtStepPlan
import com.ghostlock.app.engine.ktstep.PayloadPageInputs
import com.ghostlock.app.engine.ktstep.RouteResult
import com.ghostlock.app.engine.ktstep.VictimProbe
import com.ghostlock.app.engine.ktstep.WriteRequest

/** Adapts a Cve43499RoutePort to the KernelRoute shape used by KtStepEngine. */
class Cve43499RouteAdapter(private val port: Cve43499RoutePort) : KernelRoute {
    override fun attempt(request: WriteRequest, page: ByteArray, inputs: PayloadPageInputs): RouteResult =
        port.attempt(request, page, inputs)
}

/** One engine over a selected port; route id is already fixed by the port. */
class Cve43499RoutedEngine(
    private val port: Cve43499RoutePort,
    private val probe: VictimProbe,
) {
    suspend fun run(plan: KtStepPlan): KtStepOutcome =
        KtStepEngine(Cve43499RouteAdapter(port), probe).runSuspend(plan)
}
