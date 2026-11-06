package com.ghostlock.app.backend.backends.debugprobe

import android.os.Build
import android.os.Process
import com.ghostlock.app.core.flow.BackendFlowInput
import com.ghostlock.app.core.flow.BackendRunFacts
import com.ghostlock.app.core.flow.Flow
import com.ghostlock.app.core.flow.complete
import com.ghostlock.app.domain.run.BackendTerminalHint
import com.ghostlock.app.domain.run.DiagnosticOutcome
import com.ghostlock.app.domain.run.ExitKind
import com.ghostlock.app.domain.run.ResultReason
import com.ghostlock.app.domain.run.RunLogDraft
import com.ghostlock.app.domain.run.RunLogLevel
import com.ghostlock.app.domain.run.RunLogOrigin
import com.ghostlock.app.domain.run.RunReport
import com.ghostlock.app.domain.run.StageResult
import com.ghostlock.app.domain.run.StageState
import java.io.File
import java.util.concurrent.atomic.AtomicBoolean

/**
 * Read-only diagnostic Flow used to exercise the typed RunHost path.
 *
 * The probe emits bounded typed notices for its process identity and read-only environment, then returns
 * PASS only when the snapshot completed. It never dispatches Payload/Finish and can never represent product root.
 */
internal class DebugProbeFlow : Flow {
    private val stopRequested = AtomicBoolean(false)

    /** Runs the bounded read-only fixture and returns its final diagnostic report. */
    override suspend fun run(input: BackendFlowInput): RunReport {
        if (stopRequested.get() || input.cancellation.isCancellationRequested()) {
            return input.complete(cancelledFacts())
        }
        val facts = runCatching {
            val facts = listOf(
                "debug-probe:start",
                "debug-probe:uid=" + Process.myUid(),
                "debug-probe:pid=" + Process.myPid(),
                "debug-probe:sdk=" + Build.VERSION.SDK_INT,
                "debug-probe:device=" + Build.DEVICE,
                "debug-probe:model=" + Build.MODEL,
                "debug-probe:abi=" + Build.SUPPORTED_ABIS.joinToString(","),
                "debug-probe:domain=" + readFirstLine("/proc/self/attr/current"),
                "debug-probe:done",
            )
            facts.forEach { line ->
                if (stopRequested.get() || input.cancellation.isCancellationRequested()) {
                    return input.complete(cancelledFacts())
                }
                input.logs.emit(
                    RunLogDraft(
                        origin = RunLogOrigin.Kotlin,
                        level = RunLogLevel.Debug,
                        message = line.toAsciiBounded(),
                    ),
                )
            }
            BackendRunFacts(
                primitive = StageResult.SUCCESS,
                scriptDispatch = StageResult.SKIPPED,
                cleanup = StageResult.SUCCESS,
                terminalHint = BackendTerminalHint.NONE,
                diagnosticOutcome = DiagnosticOutcome.PASS,
            )
        }.getOrElse { problem ->
            runCatching {
                input.logs.emit(
                    RunLogDraft(
                        origin = RunLogOrigin.Kotlin,
                        level = RunLogLevel.Error,
                        message = ("debug-probe:failed:" + problem::class.java.simpleName).toAsciiBounded(),
                    ),
                )
            }
            BackendRunFacts(
                primitive = StageResult(
                    StageState.FAILED,
                    ResultReason.INTERNAL_CONTRACT_FAILURE,
                    ExitKind.NONE,
                    0,
                ),
                scriptDispatch = StageResult.SKIPPED,
                cleanup = StageResult.SUCCESS,
                terminalHint = BackendTerminalHint.NONE,
                diagnosticOutcome = DiagnosticOutcome.FAIL,
            )
        }
        return input.complete(facts)
    }

    /** Records a cooperative stop request; the probe owns no process or native resource. */
    override fun stop() {
        stopRequested.set(true)
    }

    /** Returns typed cancellation facts without claiming diagnostic PASS. */
    private fun cancelledFacts(): BackendRunFacts = BackendRunFacts(
        primitive = StageResult(StageState.CANCELLED, ResultReason.CANCELLED, ExitKind.NONE, 0),
        scriptDispatch = StageResult.SKIPPED,
        cleanup = StageResult.SUCCESS,
        terminalHint = BackendTerminalHint.NONE,
        diagnosticOutcome = DiagnosticOutcome.UNKNOWN,
    )

    /** Reads one line from a read-only proc/sys file and preserves failure as diagnostic text. */
    private fun readFirstLine(path: String): String = runCatching {
        File(path).bufferedReader().use { reader -> reader.readLine().orEmpty().trim() }
    }.getOrElse { problem -> "read-failed:" + problem::class.java.simpleName }

    /** Restricts diagnostic bodies to bounded printable ASCII and removes proc NUL/control bytes. */
    private fun String.toAsciiBounded(): String =
        filter { character -> character.code in 0x20..0x7e }.take(MAX_LOG_CHARACTERS)

    private companion object {
        const val MAX_LOG_CHARACTERS: Int = 4 * 1024
    }
}
