package com.ghostlock.app.engine.ktstep

/** K4 victim 生命周期 + KtStepEngine：start 成功才跑步骤，任何路径都 close。 */
class KtVictimFlow(
    private val route: KernelRoute,
    private val session: VictimSession,
    private val log: (String) -> Unit = {},
) {
    fun run(plan: KtStepPlan): KtStepOutcome {
        val started = session.start()
        if (started is SyscallResult.Errno) {
            session.close()
            return KtStepOutcome(
                KtEnginePhase.Failed,
                "victim-start:" + started.code,
                emptyList(),
                emptyList(),
            )
        }
        return try {
            KtStepEngine(route, session, log).run(plan)
        } finally {
            session.close()
        }
    }
}
