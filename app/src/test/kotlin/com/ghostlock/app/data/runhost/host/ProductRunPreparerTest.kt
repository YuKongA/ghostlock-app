package com.ghostlock.app.data.runhost.host

import com.ghostlock.app.core.flow.BackendRunFacts
import com.ghostlock.app.core.flow.RootScriptDecodeOutcome
import com.ghostlock.app.core.flow.RunReportComposer
import com.ghostlock.app.core.runhost.RunHostArtifactKind
import com.ghostlock.app.core.runhost.RunHostArtifactRequest
import com.ghostlock.app.core.runhost.RunHostIdentity
import com.ghostlock.app.core.runhost.RunHostPayloadAcceptance
import com.ghostlock.app.core.runhost.RunHostPayloadItemRequest
import com.ghostlock.app.core.runhost.RunHostPayloadKind
import com.ghostlock.app.core.runhost.RunHostRequestModel
import com.ghostlock.app.core.rootscript.PublishedArtifactIdentity
import com.ghostlock.app.core.rootscript.RootScriptResultCodec
import com.ghostlock.app.data.run.AndroidRunWorkspace
import com.ghostlock.app.data.run.RootArtifactStagingAdapter
import com.ghostlock.app.data.run.StagedRunInputArtifact
import com.ghostlock.app.domain.run.BackendTerminalHint
import com.ghostlock.app.domain.run.DiagnosticOutcome
import com.ghostlock.app.domain.run.ExitKind
import com.ghostlock.app.domain.run.ItemState
import com.ghostlock.app.domain.run.ResultReason
import com.ghostlock.app.domain.run.RunTerminal
import com.ghostlock.app.domain.run.StageResult
import com.ghostlock.app.domain.run.StageState
import java.nio.file.Files
import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

/** Shared product publication, direct same-plan decode, and six-layer composition checks. */
@RunWith(RobolectricTestRunner::class)
class ProductRunPreparerTest {
    @Test
    fun preparesOnePlanAndComposesCompleteGlr1WithoutLogInference() {
        val root = Files.createTempDirectory("product-pipeline").toFile()
        val workspace = AndroidRunWorkspace(
            root = root,
            stagedInputs = root.resolve("input").apply { mkdir() },
            executables = root.resolve("executable").apply { mkdir() },
            rootScript = root.resolve("root-script").apply { mkdir() },
            persistentStateDirectory = root.resolve("state").apply { mkdir() },
        )
        val profile = staged(workspace.stagedInputs.resolve("profile.bin"), "profile", RunHostArtifactKind.PROFILE, "{}")
        val manager = staged(
            workspace.executables.resolve("manager.exec"),
            "root-manager-executable",
            RunHostArtifactKind.EXECUTABLE,
            "manager",
        )
        val identity = RunHostIdentity("0123456789abcdef0123456789abcdef", 1L)
        val request = RunHostRequestModel(
            runId = identity.runId,
            generation = identity.generation,
            backendKey = "fixture",
            profileArtifactId = "profile",
            payloadItems = listOf(
                RunHostPayloadItemRequest(
                    itemId = "payload-root",
                    kind = RunHostPayloadKind.ROOT_MANAGER,
                    required = true,
                    artifactId = "root-manager-executable",
                    args = listOf("late-load", "--kmi", "android13-5.15"),
                    acceptance = RunHostPayloadAcceptance.DAEMON_ACK,
                ),
            ),
            finishItems = emptyList(),
            artifacts = listOf(profile.metadata, manager.metadata),
        )
        val pipeline = ProductRunPreparer(RootArtifactStagingAdapter())
        val prepared = pipeline.prepare(workspace, identity, request, listOf(profile, manager))
        workspace.rootScript.resolve("root-result.glr1").writeText(
            "GLR1\n" +
                "run=${identity.runId}\n" +
                "generation=1\n" +
                "payload.payload-root.state=2\n" +
                "payload.payload-root.reason=0\n" +
                "payload.payload-root.exit_kind=1\n" +
                "payload.payload-root.exit_value=0\n" +
                "handoff=0\n" +
                "complete=1\n",
        )
        val resultFile = workspace.rootScript.resolve("root-result.glr1")
        val decoded = RootScriptResultCodec.decode(resultFile, prepared.rootScript.resultIdentity, prepared.plan)
        val report = RunReportComposer().compose(
            prepared.plan,
            successfulBackendFacts(),
            RootScriptDecodeOutcome.Complete(decoded),
        )
        assertEquals(ItemState.SUCCESS, report.payloadItems.single().state)
        assertEquals(RunTerminal.SUCCESS, report.terminal)
        assertEquals(listOf("late-load", "--kmi", "android13-5.15"), prepared.plan.payload.single().args)
    }

    @Test
    fun failedDispatchSynthesizesNotStartedAndDoesNotDecodeResult() {
        val plan = com.ghostlock.app.core.rootscript.RootScriptExecutionPlan(
            runId = "0123456789abcdef0123456789abcdef",
            generation = 1L,
            payload = listOf(
                com.ghostlock.app.core.rootscript.RootScriptPayloadExecution(
                    "payload-command",
                    com.ghostlock.app.core.rootscript.RootScriptPayloadKind.COMMAND,
                    true,
                    null,
                    listOf("id"),
                    com.ghostlock.app.core.rootscript.RootScriptPayloadAcceptance.EXIT_ZERO,
                ),
            ),
            finish = emptyList(),
        )
        val report = RunReportComposer().compose(
            plan = plan,
            backend = successfulBackendFacts().copy(
                scriptDispatch = StageResult(
                    StageState.FAILED,
                    ResultReason.PROCESS_EXIT_NONZERO,
                    ExitKind.PROCESS_EXIT,
                    22,
                ),
            ),
            decode = RootScriptDecodeOutcome.NotAttempted,
        )
        assertEquals(ItemState.NOT_RUN, report.payloadItems.single().state)
        assertEquals(RunTerminal.FAILED, report.terminal)
    }

    private fun staged(
        file: java.io.File,
        logicalId: String,
        kind: RunHostArtifactKind,
        text: String,
    ): StagedRunInputArtifact {
        file.writeText(text)
        val identity = PublishedArtifactIdentity.capture(file)
        return StagedRunInputArtifact(
            metadata = RunHostArtifactRequest(logicalId, kind, identity.byteCount, identity.sha256),
            file = file,
            identity = identity,
        )
    }

    private fun successfulBackendFacts(): BackendRunFacts = BackendRunFacts(
        primitive = StageResult.SUCCESS,
        scriptDispatch = StageResult.SUCCESS,
        cleanup = StageResult.SUCCESS,
        terminalHint = BackendTerminalHint.NONE,
        diagnosticOutcome = DiagnosticOutcome.NOT_APPLICABLE,
    )
}
