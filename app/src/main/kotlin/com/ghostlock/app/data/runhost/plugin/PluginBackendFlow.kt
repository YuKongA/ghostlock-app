package com.ghostlock.app.data.runhost.plugin

import android.os.Process
import com.ghostlock.app.core.flow.BackendFlowInput
import com.ghostlock.app.core.flow.BackendRunFacts
import com.ghostlock.app.core.flow.Flow
import com.ghostlock.app.core.flow.complete
import com.ghostlock.app.domain.run.BackendTerminalHint
import com.ghostlock.app.domain.run.DiagnosticOutcome
import com.ghostlock.app.domain.run.ExitKind
import com.ghostlock.app.domain.run.LogDraft
import com.ghostlock.app.domain.run.LogLevel
import com.ghostlock.app.domain.run.LogOrigin
import com.ghostlock.app.domain.run.ResultReason
import com.ghostlock.app.domain.run.RunReport
import com.ghostlock.app.domain.run.StageResult
import com.ghostlock.app.domain.run.StageState
import com.ghostlock.plugin.api.v1.DiagnosticRunContext
import com.ghostlock.plugin.api.v1.DiagnosticRunResult
import com.ghostlock.plugin.api.v1.PluginException
import com.ghostlock.plugin.api.v1.PluginExitKind
import com.ghostlock.plugin.api.v1.PluginFailureCode
import com.ghostlock.plugin.api.v1.PluginLogLevel
import com.ghostlock.plugin.api.v1.PluginLogOrigin
import com.ghostlock.plugin.api.v1.PluginResultReason
import com.ghostlock.plugin.api.v1.PluginStageResult
import com.ghostlock.plugin.api.v1.PluginStageState
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.withContext

/** Sole Host adapter from the blocking shared diagnostic API into the existing Flow/report model. */
internal class PluginBackendFlow(
    private val resources: PluginRunResourceOwner,
) : Flow {
    override suspend fun run(input: BackendFlowInput): RunReport {
        var terminalHint = BackendTerminalHint.NONE
        val stages = try {
            val result: DiagnosticRunResult? = withContext(Dispatchers.IO) {
                resources.execute(Context(input))
            }
            if (result == null) {
                terminalHint = BackendTerminalHint.UNKNOWN_HELD_STATE
                unknownStages()
            } else {
                result.backendPrimitive().toHost() to result.rootScriptDispatch().toHost()
            }
        } catch (problem: PluginException) {
            when (problem.code()) {
                PluginFailureCode.INVALID_INPUT -> failed(ResultReason.REJECTED) to StageResult.SKIPPED
                PluginFailureCode.NOT_READY -> failed(ResultReason.NOT_READY) to StageResult.SKIPPED
                PluginFailureCode.IO_FAILURE -> failed(ResultReason.IO_FAILURE) to StageResult.SKIPPED
                PluginFailureCode.CANCELLED -> cancelled() to cancelled()
                PluginFailureCode.CONTRACT_FAILURE -> {
                    terminalHint = BackendTerminalHint.UNKNOWN_HELD_STATE
                    unknownStages()
                }
            }
        } catch (_: Throwable) {
            terminalHint = BackendTerminalHint.UNKNOWN_HELD_STATE
            unknownStages()
        }
        val cleanup = withContext(NonCancellable + Dispatchers.IO) {
            resources.closeAndGetCleanup()
        }
        val outcome = diagnosticOutcome(stages.first, stages.second, cleanup)
        return input.complete(
            BackendRunFacts(
                primitive = stages.first,
                scriptDispatch = stages.second,
                cleanup = cleanup,
                terminalHint = terminalHint,
                diagnosticOutcome = outcome,
            ),
        )
    }

    /** B1 Diagnose is BEFORE_START_ONLY; no concurrent plugin stop method exists in API revision 1. */
    override fun stop() = Unit

    private class Context(
        private val input: BackendFlowInput,
    ) : DiagnosticRunContext {
        override fun runId(): String = input.runId
        override fun generation(): Long = input.generation
        override fun processUid(): Int = Process.myUid()
        override fun processPid(): Int = Process.myPid()
        override fun cancellationRequested(): Boolean = input.cancellation.isCancellationRequested()

        override fun emit(origin: PluginLogOrigin, level: PluginLogLevel, message: String) {
            // The RunHost sink owns grammar validation and its dropped-log counter. Delivery or
            // validation failure must never escape through the shared plugin API.
            runCatching { input.logs.emit(LogDraft(origin.toHost(), level.toHost(), message)) }
        }
    }

    private companion object {
        fun failed(reason: ResultReason): StageResult =
            StageResult(StageState.FAILED, reason, ExitKind.NONE, 0)

        fun cancelled(): StageResult =
            StageResult(StageState.CANCELLED, ResultReason.CANCELLED, ExitKind.NONE, 0)

        fun unknown(): StageResult = StageResult(
            StageState.UNKNOWN,
            ResultReason.INTERNAL_CONTRACT_FAILURE,
            ExitKind.NONE,
            0,
        )

        fun unknownStages(): Pair<StageResult, StageResult> = unknown() to unknown()

        fun diagnosticOutcome(
            primitive: StageResult,
            dispatch: StageResult,
            cleanup: StageResult,
        ): DiagnosticOutcome {
            if (cleanup.state == StageState.UNKNOWN || cleanup.state == StageState.CANCELLED) {
                return DiagnosticOutcome.UNKNOWN
            }
            if (cleanup.state != StageState.SUCCESS) return DiagnosticOutcome.FAIL
            if (listOf(primitive, dispatch).any { fact ->
                    fact.state == StageState.UNKNOWN || fact.state == StageState.CANCELLED
                }
            ) {
                return DiagnosticOutcome.UNKNOWN
            }
            if (listOf(primitive, dispatch).any { it.state == StageState.FAILED }) {
                return DiagnosticOutcome.FAIL
            }
            return if (primitive.state == StageState.SUCCESS && dispatch.state == StageState.SUCCESS) {
                DiagnosticOutcome.PASS
            } else {
                DiagnosticOutcome.UNKNOWN
            }
        }
    }
}

private fun PluginStageResult.toHost(): StageResult = StageResult(
    state = when (state()) {
        PluginStageState.SUCCESS -> StageState.SUCCESS
        PluginStageState.FAILED -> StageState.FAILED
        PluginStageState.CANCELLED -> StageState.CANCELLED
        PluginStageState.UNKNOWN -> StageState.UNKNOWN
        PluginStageState.SKIPPED -> StageState.SKIPPED
    },
    reason = when (reason()) {
        PluginResultReason.NONE -> ResultReason.NONE
        PluginResultReason.NOT_STARTED -> ResultReason.NOT_STARTED
        PluginResultReason.REJECTED -> ResultReason.REJECTED
        PluginResultReason.NOT_READY -> ResultReason.NOT_READY
        PluginResultReason.IO_FAILURE -> ResultReason.IO_FAILURE
        PluginResultReason.PROCESS_EXIT_NONZERO -> ResultReason.PROCESS_EXIT_NONZERO
        PluginResultReason.SIGNALLED -> ResultReason.SIGNALLED
        PluginResultReason.CANCELLED -> ResultReason.CANCELLED
        PluginResultReason.MALFORMED_RESULT -> ResultReason.MALFORMED_RESULT
        PluginResultReason.ARTIFACT_IDENTITY_MISMATCH -> ResultReason.ARTIFACT_IDENTITY_MISMATCH
        PluginResultReason.INTERNAL_CONTRACT_FAILURE -> ResultReason.INTERNAL_CONTRACT_FAILURE
    },
    exitKind = when (exitKind()) {
        PluginExitKind.NONE -> ExitKind.NONE
        PluginExitKind.PROCESS_EXIT -> ExitKind.PROCESS_EXIT
        PluginExitKind.NATIVE_STATUS -> ExitKind.NATIVE_STATUS
    },
    exitValue = exitValue(),
)

private fun PluginLogOrigin.toHost(): LogOrigin = when (this) {
    PluginLogOrigin.KOTLIN -> LogOrigin.Kotlin
    PluginLogOrigin.NATIVE -> LogOrigin.Native
    PluginLogOrigin.SHELL -> LogOrigin.Shell
}

private fun PluginLogLevel.toHost(): LogLevel = when (this) {
    PluginLogLevel.DEBUG -> LogLevel.Debug
    PluginLogLevel.INFO -> LogLevel.Info
    PluginLogLevel.WARN -> LogLevel.Warn
    PluginLogLevel.ERROR -> LogLevel.Error
}
