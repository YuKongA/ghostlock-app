package com.ghostlock.app.backend.backends.lua

import com.ghostlock.app.core.flow.BackendFlowInput
import com.ghostlock.app.core.flow.BackendRunFacts
import com.ghostlock.app.core.flow.Flow
import com.ghostlock.app.core.flow.complete
import com.ghostlock.app.domain.run.BackendTerminalHint
import com.ghostlock.app.domain.run.DiagnosticOutcome
import com.ghostlock.app.domain.run.ExitKind
import com.ghostlock.app.domain.run.ResultReason
import com.ghostlock.app.domain.run.LogDraft
import com.ghostlock.app.domain.run.LogLevel
import com.ghostlock.app.domain.run.LogOrigin
import com.ghostlock.app.domain.run.RunReport
import com.ghostlock.app.domain.run.StageResult
import com.ghostlock.app.domain.run.StageState
import java.util.concurrent.atomic.AtomicBoolean

/**
 * Restricted Lua operation-composition Flow.
 *
 * This class consumes only a pre-parsed [LuaFlowInput] and exact [LuaOperationCatalog]. It exposes no Java
 * reflection, constructors, raw addresses, native symbols, JNA types, or arbitrary JVM object conversion.
 */
internal class FlowLuaImpl(
    private val luaInput: LuaFlowInput,
    private val catalog: LuaOperationCatalog,
) : Flow {
    private val stopRequested = AtomicBoolean(false)

    /** Executes approved coarse operations in source order and returns the final product report. */
    override suspend fun run(input: BackendFlowInput): RunReport {
        if (luaInput.calls.isEmpty()) {
            return input.complete(refused(ResultReason.NOT_READY))
        }
        var primitive = StageResult.SKIPPED
        for (call in luaInput.calls) {
            if (stopRequested.get() || input.cancellation.isCancellationRequested()) {
                return input.complete(cancelled(primitive))
            }
            val registration = catalog.resolve(call.operation)
                ?: return input.complete(refused(ResultReason.REJECTED))
            if (call.arguments.size > registration.maximumArguments) {
                return input.complete(refused(ResultReason.REJECTED))
            }
            emitNotice(input, "lua-operation:start:" + call.operation)
            primitive = runCatching {
                registration.operation.execute(input, call.arguments)
            }.getOrElse {
                StageResult(
                    StageState.UNKNOWN,
                    ResultReason.INTERNAL_CONTRACT_FAILURE,
                    ExitKind.NONE,
                    0,
                )
            }
            emitNotice(input, "lua-operation:end:" + call.operation + ":" + primitive.state.name)
            if (primitive.state != StageState.SUCCESS) break
        }
        return input.complete(
            BackendRunFacts(
                primitive = primitive,
                scriptDispatch = StageResult.SKIPPED,
                cleanup = StageResult.SUCCESS,
                terminalHint = BackendTerminalHint.NONE,
                diagnosticOutcome = DiagnosticOutcome.NOT_APPLICABLE,
            ),
        )
    }

    /** Records cooperative cancellation; individual operations remain responsible for safe stop semantics. */
    override fun stop() {
        stopRequested.set(true)
    }

    /** Returns a typed pre-dispatch refusal. */
    private fun refused(reason: ResultReason): BackendRunFacts = BackendRunFacts(
        primitive = StageResult(StageState.FAILED, reason, ExitKind.NONE, 0),
        scriptDispatch = StageResult.SKIPPED,
        cleanup = StageResult.SKIPPED,
        terminalHint = BackendTerminalHint.NONE,
        diagnosticOutcome = DiagnosticOutcome.NOT_APPLICABLE,
    )

    /** Returns cancellation facts without claiming that an in-flight operation was safely killed. */
    private fun cancelled(previous: StageResult): BackendRunFacts = BackendRunFacts(
        primitive = if (previous == StageResult.SKIPPED) {
            StageResult(StageState.CANCELLED, ResultReason.CANCELLED, ExitKind.NONE, 0)
        } else {
            previous
        },
        scriptDispatch = StageResult(StageState.CANCELLED, ResultReason.CANCELLED, ExitKind.NONE, 0),
        cleanup = StageResult.SUCCESS,
        terminalHint = BackendTerminalHint.NONE,
        diagnosticOutcome = DiagnosticOutcome.NOT_APPLICABLE,
    )

    /** Emits a bounded Kotlin debug record and ignores diagnostic-delivery failures. */
    private fun emitNotice(input: BackendFlowInput, body: String) {
        runCatching {
            input.logs.emit(
                LogDraft(
                    origin = LogOrigin.Kotlin,
                    level = LogLevel.Debug,
                    message = body.filter { character -> character.code in 0x20..0x7e }
                        .take(MAX_LOG_CHARACTERS),
                ),
            )
        }
    }

    private companion object {
        const val MAX_LOG_CHARACTERS: Int = 4 * 1024
    }
}
