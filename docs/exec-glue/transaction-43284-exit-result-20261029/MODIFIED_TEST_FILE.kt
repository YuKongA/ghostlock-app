package com.ghostlock.app.backend.cve43284glued

import com.ghostlock.app.core.flow.FlowResult
import com.ghostlock.app.core.flow.StepResult
import com.ghostlock.app.ui.backend.BackendRunUiState
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/** Verifies that 43284 reports success only when the upstream executable exits with status zero. */
class Flow43284GluedResultTest {
    @Test
    fun exitZeroPreservesExecutableEvidenceAsSuccess() {
        val evidence = mapOf("backend" to "43284Glued", "exit" to "0")

        val result = map43284ExecutableState(
            BackendRunUiState.Finished(FlowResult.Ok(emptyList(), evidence)),
        )

        assertEquals(StepResult.Done(evidence), result)
    }

    @Test
    fun nonzeroExitRemainsFailureWithOriginalReasonAndEvidence() {
        val evidence = mapOf("backend" to "43284Glued", "exit" to "1")

        val result = map43284ExecutableState(
            BackendRunUiState.Finished(
                FlowResult.Failed("exec-exit:1", "exec", emptyList(), evidence),
            ),
        )

        assertEquals(StepResult.Failed("exec-exit:1", evidence), result)
    }

    @Test
    fun refusalAndNonterminalStatesRemainFailures() {
        assertEquals(
            StepResult.Failed("missing,unauthorized"),
            map43284ExecutableState(BackendRunUiState.Refused(listOf("missing", "unauthorized"))),
        )
        assertTrue(map43284ExecutableState(BackendRunUiState.Running) is StepResult.Failed)
    }
}
