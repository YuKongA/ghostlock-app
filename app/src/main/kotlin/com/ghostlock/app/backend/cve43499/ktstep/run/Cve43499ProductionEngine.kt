package com.ghostlock.app.backend.cve43499.ktstep.run

import com.ghostlock.app.engine.ktstep.KernelFutexWords
import com.ghostlock.app.engine.ktstep.KtEnginePhase
import com.ghostlock.app.engine.ktstep.KtStepEngine
import com.ghostlock.app.engine.ktstep.KtStepOutcome
import com.ghostlock.app.engine.ktstep.KtStepPlan
import com.ghostlock.app.engine.ktstep.NativeAttemptPort
import com.ghostlock.app.engine.ktstep.NativePrepareResult
import com.ghostlock.app.engine.ktstep.VictimProbe

/**
 * Production 43499 entry once prepare/words/attempt are available.
 * It owns no native resource; the caller closes prepared/words/port/probe in reverse order.
 */
class Cve43499ProductionEngine(
    private val prepared: NativePrepareResult.Ok,
    private val words: KernelFutexWords,
    private val attempt: NativeAttemptPort,
    private val probe: VictimProbe,
    private val log: (String) -> Unit = {},
) {
    suspend fun run(plan: KtStepPlan): KtStepOutcome {
        val route = Cve43499PreparedRoute(prepared, words, plan, attempt)
        return KtStepEngine(route, probe, log).runSuspend(plan)
    }

    /** Convenience for callers that only need the phase. */
    suspend fun phase(plan: KtStepPlan): KtEnginePhase = run(plan).phase
}
