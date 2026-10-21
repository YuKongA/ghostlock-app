package com.ghostlock.app.backend.cve43499.ktstep.run

import com.ghostlock.app.engine.ktstep.KernelFutexWords
import com.ghostlock.app.engine.ktstep.KtEnginePhase
import com.ghostlock.app.engine.ktstep.KtStepOutcome
import com.ghostlock.app.engine.ktstep.KtStepPlan
import com.ghostlock.app.engine.ktstep.NativeAttemptPort
import com.ghostlock.app.engine.ktstep.NativePrepareResult
import com.ghostlock.app.engine.ktstep.ProcessVictimSession
import com.ghostlock.app.engine.ktstep.VictimProbe
import com.ghostlock.app.engine.ktstep.VictimSession
import com.ghostlock.app.engine.ktstep.VictimStartResult

/** Production victim lifecycle; production dependencies are bound via [production]. */
class Cve43499ProductionFlow(
    private val victimFactory: () -> VictimSession = { ProcessVictimSession() },
    private val runEngine: suspend (KtStepPlan, VictimProbe) -> KtStepOutcome,
) {
    suspend fun run(plan: KtStepPlan): KtStepOutcome {
        val victim = try {
            victimFactory()
        } catch (problem: Throwable) {
            return failed("victim-start:create")
        }
        try {
            when (val started = victim.startDetailed()) {
                VictimStartResult.Ok -> Unit
                is VictimStartResult.Failed -> return failed(started.reason)
            }
            return runEngine(plan, victim)
        } finally {
            runCatching { victim.close() }
        }
    }

    private fun failed(reason: String) =
        KtStepOutcome(KtEnginePhase.Failed, reason, emptyList(), emptyList(), uid = null)

    companion object {
        /** Binds prepare/words/attempt to the real production engine. */
        /** Bind an already-polymorphic route port to the shared KtStepEngine flow. */
        fun routed(
            port: Cve43499RoutePort,
            victimFactory: () -> VictimSession = { ProcessVictimSession() },
        ): Cve43499ProductionFlow = Cve43499ProductionFlow(victimFactory) { plan, probe ->
            Cve43499RoutedEngine(port, probe).run(plan)
        }

        fun production(
            prepared: NativePrepareResult.Ok,
            words: KernelFutexWords,
            attempt: NativeAttemptPort,
            victimFactory: () -> VictimSession = { ProcessVictimSession() },
            log: (String) -> Unit = {},
        ): Cve43499ProductionFlow = Cve43499ProductionFlow(victimFactory) { plan, probe ->
            Cve43499ProductionEngine(prepared, words, attempt, probe, log).run(plan)
        }
    }
}
