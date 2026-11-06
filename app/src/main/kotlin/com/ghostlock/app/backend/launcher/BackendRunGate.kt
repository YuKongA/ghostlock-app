package com.ghostlock.app.backend.launcher

import com.ghostlock.app.domain.backend.BackendAvailability
import com.ghostlock.app.domain.backend.BackendSpec
import com.ghostlock.app.domain.backend.ExecutionDomain
import com.ghostlock.app.domain.privilege.AttackExecutorKind
import com.ghostlock.app.domain.privilege.AttackExecutorState

/** Facts outside BackendSpec that affect whether one new run may start. */
data class RunPageContext(
    val backendRunInProgress: Boolean = false,
    val executor: AttackExecutorState = AttackExecutorState(AttackExecutorKind.APP, true),
    val shizukuTypedHostAvailable: Boolean = false,
)

/** Single neutral allow/deny result consumed by the run-session adapter. */
data class RunDecision(
    val allowed: Boolean,
    val reasons: List<String>,
)

/** Pure gate over immutable spec, dynamic availability, executor, and application run ownership. */
object BackendRunGate {
    /**
     * Computes one ordered fail-closed decision.
     *
     * @param spec Exact selected neutral spec, or null.
     * @param availability Resource-free dynamic readiness.
     * @param context Current executor and application ownership facts.
     */
    fun decide(
        spec: BackendSpec?,
        availability: BackendAvailability,
        context: RunPageContext = RunPageContext(),
    ): RunDecision {
        val reasons = mutableListOf<String>()
        if (spec == null) {
            reasons += "No backend is selected."
        } else if (!spec.selectable) {
            reasons += "The selected backend is disabled."
        }
        if (availability is BackendAvailability.NotReady) {
            reasons += availability.reasons.map { reason -> reason.message }
        }
        if (spec != null) {
            when (spec.executionDomain) {
                ExecutionDomain.APP -> if (context.executor.kind != AttackExecutorKind.APP) {
                    reasons += "This backend requires the app RunHost domain."
                }

                ExecutionDomain.SHIZUKU_SHELL -> {
                    if (context.executor.kind != AttackExecutorKind.SHIZUKU || !context.executor.authorized) {
                        reasons += "This backend requires an authorized Shizuku shell domain."
                    } else if (!context.shizukuTypedHostAvailable) {
                        reasons += "The typed Shizuku RunHost is not connected."
                    }
                }

                ExecutionDomain.APP_OR_SHIZUKU -> if (
                    context.executor.kind == AttackExecutorKind.SHIZUKU &&
                    (!context.executor.authorized || !context.shizukuTypedHostAvailable)
                ) {
                    reasons += "The typed Shizuku RunHost is not connected."
                }
            }
        }
        if (context.backendRunInProgress) reasons += "Another backend run is already active."
        return RunDecision(allowed = reasons.isEmpty(), reasons = reasons)
    }
}
