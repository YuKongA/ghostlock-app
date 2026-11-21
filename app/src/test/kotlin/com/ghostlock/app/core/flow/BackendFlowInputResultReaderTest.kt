package com.ghostlock.app.core.flow

import com.ghostlock.app.core.rootscript.PublishedArtifactIdentity
import com.ghostlock.app.core.rootscript.RootScriptArtifact
import com.ghostlock.app.core.rootscript.RootScriptExecutionPlan
import com.ghostlock.app.core.rootscript.RootScriptItemResult
import com.ghostlock.app.core.rootscript.RootScriptItemState
import com.ghostlock.app.core.rootscript.RootScriptPayloadAcceptance
import com.ghostlock.app.core.rootscript.RootScriptPayloadExecution
import com.ghostlock.app.core.rootscript.RootScriptPayloadKind
import com.ghostlock.app.core.rootscript.RootScriptResult
import com.ghostlock.app.core.rootscript.RootScriptResultReason
import com.ghostlock.app.core.rootscript.RootScriptExitKind
import com.ghostlock.app.domain.run.BackendTerminalHint
import com.ghostlock.app.domain.run.DiagnosticOutcome
import com.ghostlock.app.domain.run.ExitKind
import com.ghostlock.app.domain.run.LogEmitter
import com.ghostlock.app.domain.run.ProfileSnapshot
import com.ghostlock.app.domain.run.ResultReason
import com.ghostlock.app.domain.run.RunCancellationView
import com.ghostlock.app.domain.run.RunCapability
import com.ghostlock.app.domain.run.RunTerminal
import com.ghostlock.app.domain.run.StageResult
import com.ghostlock.app.domain.run.StageState
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicInteger
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Test

/** Flow completion tests for the bound one-attempt product result reader. */
class BackendFlowInputResultReaderTest {
    /** A failed dispatch never reads a physical result object. */
    @Test
    fun dispatchFailureSkipsReader() {
        val reads = AtomicInteger(0)
        val input = input(RootScriptResultReader {
            reads.incrementAndGet()
            error("reader must not run")
        })
        val report = input.complete(
            successfulFacts().copy(
                scriptDispatch = StageResult(
                    StageState.FAILED,
                    ResultReason.PROCESS_EXIT_NONZERO,
                    ExitKind.PROCESS_EXIT,
                    1,
                ),
            ),
        )
        assertEquals(0, reads.get())
        assertEquals(RunTerminal.FAILED, report.terminal)
    }

    /** Existing expected reader outcomes compose reports instead of escaping after terminal claim. */
    @Test
    fun expectedReaderFailuresRemainTypedOutcomes() {
        listOf(
            RootScriptDecodeOutcome.IdentityMismatch,
            RootScriptDecodeOutcome.Malformed,
        ).forEach { outcome ->
            val report = input(RootScriptResultReader { outcome }).complete(successfulFacts())
            assertEquals(RunTerminal.UNKNOWN, report.terminal)
        }
    }

    /** Successful dispatch invokes the bound reader exactly once. */
    @Test
    fun successfulDispatchReadsExactlyOnce() {
        val reads = AtomicInteger(0)
        val report = input(RootScriptResultReader {
            reads.incrementAndGet()
            RootScriptDecodeOutcome.Complete(validResult())
        }).complete(successfulFacts())
        assertEquals(1, reads.get())
        assertEquals(RunTerminal.SUCCESS, report.terminal)
    }

    /** Terminal ownership is claimed before reading and a lost second claim cannot read again. */
    @Test
    fun terminalClaimPrecedesReaderAndRemainsUnique() {
        val order = mutableListOf<String>()
        val claimed = AtomicBoolean(false)
        val input = input(
            reader = RootScriptResultReader {
                order += "read"
                RootScriptDecodeOutcome.Complete(validResult())
            },
            claim = FlowTerminalClaim {
                order += "claim"
                if (claimed.compareAndSet(false, true)) false else null
            },
        )
        input.complete(successfulFacts())
        assertEquals(listOf("claim", "read"), order)
        assertThrows(IllegalStateException::class.java) {
            input.complete(successfulFacts())
        }
        assertEquals(listOf("claim", "read", "claim"), order)
    }

    /** Creates one product Flow input with no physical-file dependency in core. */
    private fun input(
        reader: RootScriptResultReader,
        claim: FlowTerminalClaim = FlowTerminalClaim { false },
    ): BackendFlowInput {
        val identity = PublishedArtifactIdentity(1uL, 1uL, 0L, "0".repeat(64), 0u)
        return BackendFlowInput(
            runId = RUN_ID,
            generation = 1L,
            rootScript = RootScriptArtifact(
                RUN_ID,
                1L,
                "/root-script.sh",
                "/result.glr1",
                identity,
                identity,
                emptyList(),
            ),
            profile = ProfileSnapshot("profile", 3, "0".repeat(64)),
            logs = LogEmitter { },
            cancellation = RunCancellationView { false },
            reportContext = FlowReportContext(RunCapability.PRODUCT_ROOT, PLAN, reader),
            terminalClaim = claim,
        )
    }

    /** Returns backend facts that authorize one result read. */
    private fun successfulFacts(): BackendRunFacts = BackendRunFacts(
        primitive = StageResult.SUCCESS,
        scriptDispatch = StageResult.SUCCESS,
        cleanup = StageResult.SUCCESS,
        terminalHint = BackendTerminalHint.NONE,
        diagnosticOutcome = DiagnosticOutcome.NOT_APPLICABLE,
    )

    /** Returns one complete result matching [PLAN]. */
    private fun validResult(): RootScriptResult = RootScriptResult(
        runId = RUN_ID,
        generation = 1L,
        payloadItems = listOf(
            RootScriptItemResult(
                itemId = "payload-command",
                state = RootScriptItemState.Success,
                reason = RootScriptResultReason.None,
                exitKind = RootScriptExitKind.ProcessExit,
                exitValue = 0,
            ),
        ),
        finishItems = emptyList(),
        durableHandoff = false,
    )

    private companion object {
        const val RUN_ID = "0123456789abcdef0123456789abcdef"
        val PLAN = RootScriptExecutionPlan(
            runId = RUN_ID,
            generation = 1L,
            payload = listOf(
                RootScriptPayloadExecution(
                    itemId = "payload-command",
                    kind = RootScriptPayloadKind.COMMAND,
                    required = true,
                    stagedPath = null,
                    args = listOf("id"),
                    acceptance = RootScriptPayloadAcceptance.EXIT_ZERO,
                ),
            ),
            finish = emptyList(),
        )
    }
}
