package com.ghostlock.app.backend.transport

import com.ghostlock.app.domain.run.DiagnosticOutcome
import com.ghostlock.app.domain.run.ExitKind
import com.ghostlock.app.domain.run.ResultReason
import com.ghostlock.app.domain.run.RunCapability
import com.ghostlock.app.domain.run.RunTerminal
import com.ghostlock.app.domain.run.StageResult
import com.ghostlock.app.domain.run.StageState
import org.junit.Assert.assertFalse
import org.junit.Assert.assertThrows
import org.junit.Test

/** Pure contract cases for the immutable six-layer RunHost snapshot. */
class RunHostSnapshotContractTest {
    /** A completed diagnostic fixture never upgrades to product success. */
    @Test
    fun diagnosticTerminalIsNotProductSuccess() {
        val snapshot = diagnosticSnapshot()
        assertFalse(snapshot.toRunReport().isProductSuccess)
    }

    /** Capability, outcome, and terminal combinations outside the wire table fail closed. */
    @Test
    fun diagnosticCapabilityRejectsProductSuccessTerminal() {
        assertThrows(IllegalArgumentException::class.java) {
            diagnosticSnapshot().copy(terminal = RunTerminal.SUCCESS)
        }
    }

    /** Illegal state/reason/exit conjunctions fail at snapshot construction. */
    @Test
    fun stageConjunctionRejectsExitMismatch() {
        assertThrows(IllegalArgumentException::class.java) {
            diagnosticSnapshot().copy(
                backendPrimitive = StageResult(
                    state = StageState.SUCCESS,
                    reason = ResultReason.NONE,
                    exitKind = ExitKind.PROCESS_EXIT,
                    exitValue = 1,
                ),
            )
        }
    }

    /** A positive source sequence above the old fixed count remains valid. */
    @Test
    fun snapshotAcceptsLargePositiveSequence() {
        diagnosticSnapshot().copy(logSequence = 16_384L)
        assertThrows(IllegalArgumentException::class.java) {
            diagnosticSnapshot().copy(logSequence = -1L)
        }
    }

    /** Returns one legal completed diagnostic fixture snapshot. */
    private fun diagnosticSnapshot(): RunHostSnapshotModel = RunHostSnapshotModel(
        identity = RunHostIdentity("0".repeat(32), 1L),
        state = RunHostState.TERMINAL,
        lastCode = RunHostCode.OK,
        runCapability = RunCapability.DIAGNOSTIC_ONLY,
        diagnosticOutcome = DiagnosticOutcome.PASS,
        backendPrimitive = StageResult.SUCCESS,
        scriptDispatch = StageResult.SKIPPED,
        payloadItems = emptyList(),
        payloadOverall = StageResult.SKIPPED,
        finishItems = emptyList(),
        finishOverall = StageResult.SKIPPED,
        backendCleanup = StageResult.SUCCESS,
        terminal = RunTerminal.DIAGNOSTIC_COMPLETE,
        logSequence = 0L,
        droppedLogs = 0L,
    )
}
