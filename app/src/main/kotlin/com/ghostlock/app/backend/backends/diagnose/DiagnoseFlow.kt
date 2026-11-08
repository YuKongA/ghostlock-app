package com.ghostlock.app.backend.backends.diagnose

import com.ghostlock.app.backend.backends.diagnose.jna.DiagnoseNativeBinding
import com.ghostlock.app.core.flow.BackendFlowInput
import com.ghostlock.app.core.flow.FlowStage
import com.ghostlock.app.core.flow.FlowStep
import com.ghostlock.app.core.flow.FlowStepFacts
import com.ghostlock.app.core.flow.FlowStepRun
import com.ghostlock.app.core.flow.SequentialFlow
import com.ghostlock.app.core.flow.StepResult
import com.ghostlock.app.domain.run.DiagnosticOutcome
import com.ghostlock.app.domain.run.ExitKind
import com.ghostlock.app.domain.run.LogDraft
import com.ghostlock.app.domain.run.LogLevel
import com.ghostlock.app.domain.run.LogOrigin
import com.ghostlock.app.domain.run.ResultReason
import com.ghostlock.app.domain.run.StageResult
import com.ghostlock.app.domain.run.StageState

/**
 * Diagnostic-only Flow whose sole physical step performs JNA, Native/Shell logging, and common handoff.
 *
 * The default constructor creates [DiagnoseNativeBinding] immediately, so constant-name DSO loading remains in
 * RunHost prepare. The synchronous native call owns shell spawn/reap; Kotlin owns no Process or script path.
 */
internal class DiagnoseFlow internal constructor(
    private val binding: DiagnoseNativeBinding = DiagnoseNativeBinding(),
    private val currentUid: () -> Int = { android.os.Process.myUid() },
    private val currentPid: () -> Int = { android.os.Process.myPid() },
    private val nonceFactory: () -> Long = { System.nanoTime() },
) : SequentialFlow() {
    /** Declares one physical operation that owns both primitive and root-script dispatch facts. */
    override fun steps(input: BackendFlowInput): List<FlowStep> = listOf(
        FlowStep(
            name = CHAIN_STEP,
            produces = setOf(FlowStage.BACKEND_PRIMITIVE, FlowStage.ROOT_SCRIPT_DISPATCH),
            step = { context -> runNativeChain(context.input) },
        ),
    )

    /** Closes only binding-owned pipe endpoints; native has already reaped any handoff child before return. */
    override suspend fun cleanup(input: BackendFlowInput): StageResult = runCatching {
        binding.close()
        StageResult.SUCCESS
    }.getOrElse {
        StageResult(
            StageState.FAILED,
            ResultReason.CLEANUP_INCOMPLETE,
            ExitKind.NONE,
            0,
        )
    }

    /** Derives Diagnose outcome from two independent facts produced by the one physical chain. */
    override fun diagnosticOutcome(
        runs: List<FlowStepRun>,
        cleanupFact: StageResult,
    ): DiagnosticOutcome {
        if (cleanupFact.state in setOf(StageState.UNKNOWN, StageState.CANCELLED)) {
            return DiagnosticOutcome.UNKNOWN
        }
        if (cleanupFact.state != StageState.SUCCESS) return DiagnosticOutcome.FAIL
        val facts = runs.singleOrNull { run -> run.name == CHAIN_STEP }?.facts
            ?: return DiagnosticOutcome.UNKNOWN
        val primitive = facts.primitive ?: return DiagnosticOutcome.UNKNOWN
        val dispatch = facts.scriptDispatch ?: return DiagnosticOutcome.UNKNOWN
        if (listOf(primitive, dispatch).any { fact ->
                fact.state in setOf(StageState.UNKNOWN, StageState.CANCELLED)
            }
        ) {
            return DiagnosticOutcome.UNKNOWN
        }
        if (listOf(primitive, dispatch).any { fact -> fact.state == StageState.FAILED }) {
            return DiagnosticOutcome.FAIL
        }
        return if (primitive.state == StageState.SUCCESS && dispatch.state == StageState.SUCCESS) {
            DiagnosticOutcome.PASS
        } else {
            DiagnosticOutcome.UNKNOWN
        }
    }

    /** Executes the one synchronous JNA/native/handoff chain and maps each phase independently. */
    private suspend fun runNativeChain(input: BackendFlowInput): StepResult {
        if (input.cancellation.isCancellationRequested()) {
            val cancelled = StageResult(StageState.CANCELLED, ResultReason.CANCELLED, ExitKind.NONE, 0)
            return StepResult.Failed(
                FlowStepFacts(primitive = cancelled, scriptDispatch = cancelled),
                "chain-cancelled-before-call",
            )
        }
        emit(input, LogOrigin.Kotlin, LogLevel.Info, "diagnose:start")
        val nonce = nonceFactory()
        val chain = runCatching { binding.runChain(input.logs, nonce) }.getOrElse { problem ->
            return StepResult.Failed(
                FlowStepFacts(
                    primitive = StageResult(StageState.FAILED, ResultReason.IO_FAILURE, ExitKind.NONE, 0),
                    scriptDispatch = StageResult.SKIPPED,
                ),
                "native-chain-failed:" + problem::class.java.simpleName,
            )
        }
        if (chain.status != DiagnoseNativeBinding.STATUS_OK) {
            return StepResult.Failed(
                FlowStepFacts(
                    primitive = StageResult(
                        StageState.FAILED,
                        ResultReason.REJECTED,
                        ExitKind.NATIVE_STATUS,
                        chain.status,
                    ),
                    scriptDispatch = StageResult.SKIPPED,
                ),
                "native-status",
            )
        }
        if (chain.uid != currentUid() || chain.pid != currentPid() || chain.echoedNonce != nonce) {
            return StepResult.Failed(
                FlowStepFacts(
                    primitive = internalUnknown(),
                    scriptDispatch = StageResult.SKIPPED,
                ),
                "native-contract-mismatch",
            )
        }

        val primitive = if (chain.nativeLogValid) {
            StageResult.SUCCESS
        } else {
            malformedResult()
        }
        val dispatch = dispatchFact(chain.handoffStatus, chain.shellLogValid)
        val facts = FlowStepFacts(primitive = primitive, scriptDispatch = dispatch)
        return if (primitive.state == StageState.SUCCESS && dispatch.state == StageState.SUCCESS) {
            StepResult.Done(facts)
        } else {
            StepResult.Failed(facts, "diagnose-chain-phase-failed")
        }
    }

    /** Maps the common helper return without conflating call status and dispatch status. */
    private fun dispatchFact(handoffStatus: Int, shellLogValid: Boolean): StageResult = when {
        handoffStatus == DiagnoseNativeBinding.HANDOFF_NOT_RUN -> StageResult.SKIPPED
        handoffStatus == 0 && shellLogValid -> StageResult(
            StageState.SUCCESS,
            ResultReason.NONE,
            ExitKind.PROCESS_EXIT,
            0,
        )
        handoffStatus == 0 -> malformedResult()
        handoffStatus in 1..255 -> StageResult(
            StageState.FAILED,
            ResultReason.PROCESS_EXIT_NONZERO,
            ExitKind.PROCESS_EXIT,
            handoffStatus,
        )
        handoffStatus == DiagnoseNativeBinding.HANDOFF_CHILD_SIGNALED -> StageResult(
            StageState.FAILED,
            ResultReason.SIGNALLED,
            ExitKind.NATIVE_STATUS,
            handoffStatus,
        )
        handoffStatus in KNOWN_HELPER_FAILURES -> StageResult(
            StageState.FAILED,
            if (handoffStatus in ARTIFACT_HELPER_FAILURES) {
                ResultReason.ARTIFACT_IDENTITY_MISMATCH
            } else {
                ResultReason.IO_FAILURE
            },
            ExitKind.NATIVE_STATUS,
            handoffStatus,
        )
        else -> StageResult(
            StageState.UNKNOWN,
            ResultReason.INTERNAL_CONTRACT_FAILURE,
            ExitKind.NATIVE_STATUS,
            handoffStatus,
        )
    }

    /** Emits one bounded source record while keeping logs outside result authority. */
    private fun emit(
        input: BackendFlowInput,
        origin: LogOrigin,
        level: LogLevel,
        message: String,
    ) {
        runCatching { input.logs.emit(LogDraft(origin, level, message)) }
    }

    /** @return Stable malformed-result fact for one phase's invalid record. */
    private fun malformedResult(): StageResult = StageResult(
        StageState.UNKNOWN,
        ResultReason.MALFORMED_RESULT,
        ExitKind.NONE,
        0,
    )

    /** @return Stable internal-contract fact for an untrusted successful-call payload. */
    private fun internalUnknown(): StageResult = StageResult(
        StageState.UNKNOWN,
        ResultReason.INTERNAL_CONTRACT_FAILURE,
        ExitKind.NONE,
        0,
    )

    private companion object {
        const val CHAIN_STEP: String = "diagnose-native-chain"

        val ARTIFACT_HELPER_FAILURES: Set<Int> = setOf(
            DiagnoseNativeBinding.HANDOFF_SCRIPT_TYPE,
            DiagnoseNativeBinding.HANDOFF_SCRIPT_MAGIC,
        )
        val KNOWN_HELPER_FAILURES: Set<Int> = setOf(
            DiagnoseNativeBinding.HANDOFF_HOME_MISSING,
            DiagnoseNativeBinding.HANDOFF_HOME_INVALID,
            DiagnoseNativeBinding.HANDOFF_OPEN_HOME,
            DiagnoseNativeBinding.HANDOFF_OPEN_DIRECTORY,
            DiagnoseNativeBinding.HANDOFF_OPEN_SCRIPT,
            DiagnoseNativeBinding.HANDOFF_SCRIPT_TYPE,
            DiagnoseNativeBinding.HANDOFF_SCRIPT_MAGIC,
            DiagnoseNativeBinding.HANDOFF_SPAWN_CALL,
            DiagnoseNativeBinding.HANDOFF_WAIT,
            DiagnoseNativeBinding.HANDOFF_INTERNAL,
        )
    }
}
