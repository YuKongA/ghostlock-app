package com.ghostlock.app.backend.cve43499.ktstep.run

import com.ghostlock.app.engine.ktstep.KtEnginePhase
import com.ghostlock.app.engine.ktstep.KtStepOutcome
import com.ghostlock.app.engine.ktstep.KtStepPlan
import com.ghostlock.app.engine.ktstep.NativePrepareParams

/** One-shot production entry: open -> run -> close; no UI/config dependency. */
object Cve43499ProductionRunner {
    suspend fun run(params: NativePrepareParams, plan: KtStepPlan): KtStepOutcome {
        return when (val opened = Cve43499ProductionSession.open(params)) {
            is ProductionSessionResult.Failed -> KtStepOutcome(
                KtEnginePhase.Failed, "session:" + opened.reason, emptyList(), emptyList(), uid = null,
            )
            is ProductionSessionResult.Ok -> try {
                opened.session.run(plan)
            } finally {
                opened.session.close()
            }
        }
    }
}
