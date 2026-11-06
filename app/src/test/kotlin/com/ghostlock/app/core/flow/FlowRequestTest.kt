package com.ghostlock.app.core.flow

import com.ghostlock.app.core.rootscript.PublishedArtifactIdentity
import com.ghostlock.app.core.rootscript.RootScriptArtifact
import com.ghostlock.app.domain.run.ProfileSnapshot
import com.ghostlock.app.domain.run.RunCancellationView
import com.ghostlock.app.domain.run.RunCapability
import com.ghostlock.app.domain.run.RunLogDraft
import com.ghostlock.app.domain.run.RunLogEmitter
import com.ghostlock.app.domain.run.StageResult
import com.ghostlock.app.domain.run.StageState
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/** Uniform Flow input and one-shot sequential execution contract. */
class FlowRequestTest {
    /** FlowRequest wraps only the neutral frozen input. */
    @Test
    fun requestCarriesUniformInput() {
        val input = input()
        assertEquals(input, FlowRequest(input).input)
    }

    /** A fresh sequential Flow returns one typed report, emits logs, and rejects reuse. */
    @Test
    fun sequentialFlowIsOneShotAndReturnsFacts() = runBlocking {
        val records = mutableListOf<RunLogDraft>()
        val input = input(records)
        val flow = object : SequentialFlow() {
            override fun steps(input: BackendFlowInput): List<FlowStep> = listOf(
                FlowStep(
                    name = "primitive",
                    stage = FlowStage.BACKEND_PRIMITIVE,
                    step = Step { StepResult.Done(StageResult.SUCCESS) },
                ),
            )
        }
        val report = flow.run(input)
        assertEquals(StageState.SUCCESS, report.backendPrimitive.state)
        assertTrue(records.isNotEmpty())
        assertTrue(runCatching { flow.run(input) }.isFailure)
    }

    /** @return One diagnostic Flow input whose terminal claim succeeds exactly once. */
    private fun input(records: MutableList<RunLogDraft> = mutableListOf()): BackendFlowInput {
        val identity = PublishedArtifactIdentity(
            device = 1uL,
            inode = 1uL,
            byteCount = 0L,
            sha256 = "0".repeat(64),
            mode = 0u,
        )
        return BackendFlowInput(
            runId = "0".repeat(32),
            generation = 1L,
            rootScript = RootScriptArtifact(
                runId = "0".repeat(32),
                generation = 1L,
                scriptPath = "/run/script",
                resultPath = "/run/result",
                scriptIdentity = identity,
                resultIdentity = identity,
                retainedArtifacts = emptyList(),
            ),
            profile = ProfileSnapshot("profile", ProfileSnapshot.CURRENT_SCHEMA_VERSION, "0".repeat(64)),
            logs = RunLogEmitter { record -> records += record },
            cancellation = RunCancellationView { false },
            reportContext = FlowReportContext(RunCapability.DIAGNOSTIC_ONLY, null),
            terminalClaim = FlowTerminalClaim { false },
        )
    }
}
