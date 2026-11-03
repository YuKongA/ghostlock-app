package com.ghostlock.app.backend.cve43499glued

import com.ghostlock.app.core.flow.FlowResult
import com.ghostlock.app.core.flow.StepResult
import com.ghostlock.app.ui.backend.BackendRunUiState
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/** Verifies that 43499 propagates the executable terminal result and selected profile identity. */
class Flow43499GluedResultTest {
    @Test
    fun exitZeroPreservesExecutableEvidenceAndSelectedProfile() {
        val evidence = mapOf("backend" to "43499-Glued", "exit" to "0", "runDir" to "fixture")

        val result = map43499ExecutableState(
            BackendRunUiState.Finished(FlowResult.Ok(emptyList(), evidence)),
            "6.6.30-profile",
        )

        assertEquals(
            StepResult.Done(evidence + ("profile" to "6.6.30-profile")),
            result,
        )
    }

    @Test
    fun nonzeroExitPreservesFailureReasonAndExecutableEvidence() {
        val evidence = mapOf("backend" to "43499-Glued", "exit" to "13", "runDir" to "fixture")

        val result = map43499ExecutableState(
            BackendRunUiState.Finished(
                FlowResult.Failed("exec-exit:13", "exec", emptyList(), evidence),
            ),
            "6.6.30-profile",
        )

        assertEquals(
            StepResult.Failed("exec-exit:13", evidence + ("profile" to "6.6.30-profile")),
            result,
        )
    }

    @Test
    fun refusalAndNonterminalStatesRemainFailures() {
        assertEquals(
            StepResult.Failed("missing,unauthorized"),
            map43499ExecutableState(BackendRunUiState.Refused(listOf("missing", "unauthorized")), "profile"),
        )
        assertTrue(map43499ExecutableState(BackendRunUiState.Running, "profile") is StepResult.Failed)
    }
}
