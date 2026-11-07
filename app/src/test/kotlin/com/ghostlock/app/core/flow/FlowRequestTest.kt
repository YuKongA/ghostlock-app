package com.ghostlock.app.core.flow

import com.ghostlock.app.core.rootscript.PublishedArtifactIdentity
import com.ghostlock.app.core.rootscript.RootScriptArtifact
import com.ghostlock.app.domain.run.ProfileSnapshot
import com.ghostlock.app.domain.run.RunCancellationView
import com.ghostlock.app.domain.run.RunCapability
import com.ghostlock.app.domain.run.LogDraft
import com.ghostlock.app.domain.run.LogEmitter
import com.ghostlock.app.domain.run.StageResult
import com.ghostlock.app.domain.run.StageState
import java.util.concurrent.atomic.AtomicBoolean
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
        val records = mutableListOf<LogDraft>()
        val input = input(records)
        val flow = object : SequentialFlow() {
            override fun steps(input: BackendFlowInput): List<FlowStep> = listOf(
                FlowStep(
                    name = "primitive",
                    produces = setOf(FlowStage.BACKEND_PRIMITIVE),
                    step = Step {
                        StepResult.Done(
                            FlowStepFacts.single(FlowStage.BACKEND_PRIMITIVE, StageResult.SUCCESS),
                        )
                    },
                ),
            )
        }
        val report = flow.run(input)
        assertEquals(StageState.SUCCESS, report.backendPrimitive.state)
        assertTrue(records.isNotEmpty())
        assertTrue(runCatching { flow.run(input) }.isFailure)
    }

    /** One physical operation may atomically produce primitive and dispatch facts. */
    @Test
    fun oneStepCanProduceBothBackendFacts() = runBlocking {
        val flow = object : SequentialFlow() {
            override fun steps(input: BackendFlowInput): List<FlowStep> = listOf(
                FlowStep(
                    name = "native-chain",
                    produces = FlowStage.entries.toSet(),
                    step = Step {
                        StepResult.Done(
                            FlowStepFacts(
                                primitive = StageResult.SUCCESS,
                                scriptDispatch = StageResult.SUCCESS,
                            ),
                        )
                    },
                ),
            )
        }

        val report = flow.run(input())
        assertEquals(StageState.SUCCESS, report.backendPrimitive.state)
        assertEquals(StageState.SUCCESS, report.rootScriptDispatch.state)
    }

    /** Duplicate planned writers fail before either physical operation starts. */
    @Test
    fun duplicateStageProducerFailsBeforeExecution() = runBlocking {
        var calls = 0
        val flow = object : SequentialFlow() {
            override fun steps(input: BackendFlowInput): List<FlowStep> = listOf("first", "second").map { name ->
                FlowStep(
                    name = name,
                    produces = setOf(FlowStage.BACKEND_PRIMITIVE),
                    step = Step {
                        calls += 1
                        StepResult.Done(
                            FlowStepFacts.single(FlowStage.BACKEND_PRIMITIVE, StageResult.SUCCESS),
                        )
                    },
                )
            }
        }

        val report = flow.run(input())
        assertEquals(0, calls)
        assertEquals(StageState.UNKNOWN, report.backendPrimitive.state)
        assertEquals(StageState.UNKNOWN, report.rootScriptDispatch.state)
    }

    /** Missing or undeclared returned facts become exact internal-contract failures. */
    @Test
    fun returnedFactsMustExactlyMatchDeclaredProducers() = runBlocking {
        val flow = object : SequentialFlow() {
            override fun steps(input: BackendFlowInput): List<FlowStep> = listOf(
                FlowStep(
                    name = "incomplete",
                    produces = FlowStage.entries.toSet(),
                    step = Step {
                        StepResult.Done(
                            FlowStepFacts.single(FlowStage.BACKEND_PRIMITIVE, StageResult.SUCCESS),
                        )
                    },
                ),
            )
        }

        val report = flow.run(input())
        assertEquals(StageState.UNKNOWN, report.backendPrimitive.state)
        assertEquals(StageState.UNKNOWN, report.rootScriptDispatch.state)
    }

    /** Cancellation before one dual-producer step marks both owned facts and never invokes it. */
    @Test
    fun cancellationBeforeDualProducerCancelsBothFacts() = runBlocking {
        var calls = 0
        val flow = object : SequentialFlow() {
            override fun steps(input: BackendFlowInput): List<FlowStep> = listOf(
                FlowStep(
                    name = "cancelled-chain",
                    produces = FlowStage.entries.toSet(),
                    step = Step {
                        calls += 1
                        StepResult.Done(FlowStepFacts.forStages(FlowStage.entries.toSet(), StageResult.SUCCESS))
                    },
                ),
            )
        }

        val report = flow.run(input(cancellation = AtomicBoolean(true)))
        assertEquals(0, calls)
        assertEquals(StageState.CANCELLED, report.backendPrimitive.state)
        assertEquals(StageState.CANCELLED, report.rootScriptDispatch.state)
    }

    /** An unexpected exception assigns the same internal unknown fact to every owned stage. */
    @Test
    fun unexpectedDualProducerFailureCoversBothFacts() = runBlocking {
        val flow = object : SequentialFlow() {
            override fun steps(input: BackendFlowInput): List<FlowStep> = listOf(
                FlowStep(
                    name = "throwing-chain",
                    produces = FlowStage.entries.toSet(),
                    step = Step { error("fixture") },
                ),
            )
        }

        val report = flow.run(input())
        assertEquals(StageState.UNKNOWN, report.backendPrimitive.state)
        assertEquals(StageState.UNKNOWN, report.rootScriptDispatch.state)
    }

    /** @return One diagnostic Flow input whose terminal claim succeeds exactly once. */
    private fun input(
        records: MutableList<LogDraft> = mutableListOf(),
        cancellation: AtomicBoolean = AtomicBoolean(false),
    ): BackendFlowInput {
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
            logs = LogEmitter { record -> records += record },
            cancellation = RunCancellationView { cancellation.get() },
            reportContext = FlowReportContext(RunCapability.DIAGNOSTIC_ONLY, null),
            terminalClaim = FlowTerminalClaim { false },
        )
    }
}
