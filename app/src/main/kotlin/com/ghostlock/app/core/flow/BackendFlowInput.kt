package com.ghostlock.app.core.flow

import com.ghostlock.app.core.rootscript.RootScriptArtifact
import com.ghostlock.app.core.rootscript.RootScriptExecutionPlan
import com.ghostlock.app.core.rootscript.RootScriptResultCodec
import com.ghostlock.app.domain.run.BackendTerminalHint
import com.ghostlock.app.domain.run.DiagnosticOutcome
import com.ghostlock.app.domain.run.ExitKind
import com.ghostlock.app.domain.run.ProfileSnapshot
import com.ghostlock.app.domain.run.ResultReason
import com.ghostlock.app.domain.run.RunCancellationView
import com.ghostlock.app.domain.run.RunCapability
import com.ghostlock.app.domain.run.RunLogEmitter
import com.ghostlock.app.domain.run.RunReport
import com.ghostlock.app.domain.run.RunTerminal
import com.ghostlock.app.domain.run.StageResult
import com.ghostlock.app.domain.run.StageState
import java.io.File

/**
 * Pure report inputs frozen before one Flow starts.
 *
 * @property runCapability Immutable registry capability.
 * @property productPlan Exact plan rendered into the root script for PRODUCT_ROOT; absent for diagnostics.
 */
data class FlowReportContext(
    val runCapability: RunCapability,
    val productPlan: RootScriptExecutionPlan?,
) {
    init {
        require(runCapability != RunCapability.UNRESOLVED) { "Flow capability is unresolved" }
        require((runCapability == RunCapability.PRODUCT_ROOT) == (productPlan != null)) {
            "Flow product plan disagrees with capability"
        }
    }
}

/** Sole neutral terminal/cancellation linearization port supplied by the owning RunHost state machine. */
fun interface FlowTerminalClaim {
    /**
     * Claims RUNNING to TERMINAL exactly once.
     *
     * @return Whether cancellation won before this claim, or null when terminal ownership was lost.
     */
    fun claim(): Boolean?
}

/**
 * Uniform context-free input supplied to a fresh backend [Flow].
 *
 * @property runId Stable run identity.
 * @property generation Positive run generation.
 * @property rootScript Host-local published script/result contract.
 * @property profile Frozen canonical profile identity.
 * @property logs Typed log producer boundary.
 * @property cancellation Read-only cancellation view governed by BackendSpec abort policy.
 * @property reportContext Immutable inputs required to compose the final typed report.
 * @property terminalClaim Sole neutral RunHost lifecycle linearization port.
 * @property artifacts Host-published staged artifacts keyed by exact logical identity.
 * @property persistentStateDirectory Stable backend-private state directory, when declared by the host.
 */
data class BackendFlowInput(
    val runId: String,
    val generation: Long,
    val rootScript: RootScriptArtifact,
    val profile: ProfileSnapshot,
    val logs: RunLogEmitter,
    val cancellation: RunCancellationView,
    val reportContext: FlowReportContext,
    val terminalClaim: FlowTerminalClaim,
    val artifacts: Map<String, PublishedFlowArtifact> = emptyMap(),
    val persistentStateDirectory: String? = null,
)

/**
 * Claims terminal ownership and composes the sole final report from backend facts.
 *
 * @param facts Backend primitive, dispatch, cleanup, held-state, and diagnostic facts.
 * @return Final typed report after the RunHost cancellation decision is linearized.
 * @throws IllegalStateException When this Flow no longer owns the terminal transition.
 */
fun BackendFlowInput.complete(facts: BackendRunFacts): RunReport {
    val cancelled = checkNotNull(terminalClaim.claim()) { "Flow terminal ownership was lost" }
    return when (reportContext.runCapability) {
        RunCapability.DIAGNOSTIC_ONLY -> diagnosticReport(facts, cancelled)
        RunCapability.PRODUCT_ROOT -> productReport(facts, cancelled)
        RunCapability.UNRESOLVED -> error("Flow capability is unresolved")
    }
}

/**
 * Completes an uncaught Flow failure through the same terminal claim and report composition path.
 *
 * @return Final typed failure report.
 */
fun BackendFlowInput.completeFailure(): RunReport = complete(
    BackendRunFacts(
        primitive = StageResult(
            StageState.UNKNOWN,
            ResultReason.INTERNAL_CONTRACT_FAILURE,
            ExitKind.NONE,
            0,
        ),
        scriptDispatch = StageResult.SKIPPED,
        cleanup = StageResult(
            StageState.FAILED,
            ResultReason.CLEANUP_INCOMPLETE,
            ExitKind.NONE,
            0,
        ),
        terminalHint = BackendTerminalHint.UNKNOWN_HELD_STATE,
        diagnosticOutcome = DiagnosticOutcome.UNKNOWN,
    ),
)

/** @return One diagnostic report without Payload or Finish execution. */
private fun BackendFlowInput.diagnosticReport(
    facts: BackendRunFacts,
    cancelled: Boolean,
): RunReport {
    val outcome = if (cancelled) DiagnosticOutcome.UNKNOWN else facts.diagnosticOutcome
    return RunReport(
        runCapability = RunCapability.DIAGNOSTIC_ONLY,
        diagnosticOutcome = outcome,
        backendPrimitive = facts.primitive,
        rootScriptDispatch = facts.scriptDispatch,
        payloadItems = emptyList(),
        payloadOverall = StageResult.SKIPPED,
        finishItems = emptyList(),
        finishOverall = StageResult.SKIPPED,
        backendCleanup = facts.cleanup,
        terminal = if (cancelled) {
            RunTerminal.CANCELLED
        } else when (outcome) {
            DiagnosticOutcome.PASS, DiagnosticOutcome.FAIL -> RunTerminal.DIAGNOSTIC_COMPLETE
            DiagnosticOutcome.NOT_RUN -> RunTerminal.CANCELLED
            DiagnosticOutcome.UNKNOWN, DiagnosticOutcome.NOT_APPLICABLE -> RunTerminal.UNKNOWN
        },
    )
}

/** @return One product report after the sole strict root-result decode attempt. */
private fun BackendFlowInput.productReport(
    facts: BackendRunFacts,
    cancelled: Boolean,
): RunReport {
    val plan = requireNotNull(reportContext.productPlan) { "Flow product plan is missing" }
    val decode = if (facts.scriptDispatch.state != StageState.SUCCESS) {
        RootScriptDecodeOutcome.NotAttempted
    } else {
        val resultFile = File(rootScript.resultPath)
        if (!rootScript.resultIdentity.matchesObject(resultFile)) {
            RootScriptDecodeOutcome.IdentityMismatch
        } else {
            runCatching {
                RootScriptResultCodec.decode(
                    file = resultFile,
                    identity = rootScript.resultIdentity,
                    plan = plan,
                )
            }.fold(
                onSuccess = RootScriptDecodeOutcome::Complete,
                onFailure = { RootScriptDecodeOutcome.Malformed },
            )
        }
    }
    return RunReportComposer().compose(plan, facts, decode, cancelled)
}
