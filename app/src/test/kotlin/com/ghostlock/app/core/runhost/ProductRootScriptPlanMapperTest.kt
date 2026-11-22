package com.ghostlock.app.core.runhost

import com.ghostlock.app.core.flow.PublishedFlowArtifact
import com.ghostlock.app.core.rootscript.PublishedArtifactIdentity
import com.ghostlock.app.core.rootscript.RootScriptFinishKind
import com.ghostlock.app.core.rootscript.RootScriptPayloadAcceptance
import com.ghostlock.app.core.rootscript.RootScriptPayloadKind
import com.ghostlock.app.domain.run.RunInputArtifactKind
import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Test

/** Pure request-to-plan mapping tests with exact staged artifact resolution. */
class ProductRootScriptPlanMapperTest {
    /** Ordered Payload and Finish fields map without filesystem access or token rewriting. */
    @Test
    fun mapsExactOrderedPlanAndArtifactPaths() {
        val request = request()
        val plan = ProductRootScriptPlanMapper().map(IDENTITY, request, artifacts())

        assertEquals(IDENTITY.runId, plan.runId)
        assertEquals(IDENTITY.generation, plan.generation)
        assertEquals(
            listOf(RootScriptPayloadKind.COMMAND, RootScriptPayloadKind.SCRIPT),
            plan.payload.map { item -> item.kind },
        )
        assertEquals(listOf("echo", "two words"), plan.payload.first().args)
        assertEquals("/workspace/script", plan.payload.last().stagedPath)
        assertEquals(RootScriptPayloadAcceptance.EXIT_ZERO, plan.payload.last().acceptance)
        assertEquals(RootScriptFinishKind.SCRIPT, plan.finish.single().kind)
        assertEquals("/workspace/finish", plan.finish.single().stagedPath)
        assertEquals(listOf("--final"), plan.finish.single().args)
        assertEquals(emptyList<String>(), plan.retainedArtifacts)
    }

    /** Missing referenced Payload or Finish objects fail before publication. */
    @Test
    fun rejectsMissingReferencedArtifacts() {
        val mapper = ProductRootScriptPlanMapper()
        assertThrows(IllegalArgumentException::class.java) {
            mapper.map(IDENTITY, request(), artifacts() - "payload-script")
        }
        assertThrows(IllegalArgumentException::class.java) {
            mapper.map(IDENTITY, request(), artifacts() - "finish-script")
        }
    }

    /** Returns one structurally valid request with two artifact-backed operations. */
    private fun request(): RunHostRequestModel = RunHostRequestModel(
        runId = IDENTITY.runId,
        generation = IDENTITY.generation,
        backendKey = "fixture",
        pluginContentSha256 = "a".repeat(64),
        profileArtifactId = "profile",
        payloadItems = listOf(
            RunHostPayloadItemRequest(
                itemId = "payload-command",
                kind = RunHostPayloadKind.COMMAND,
                required = true,
                artifactId = null,
                args = listOf("echo", "two words"),
                acceptance = RunHostPayloadAcceptance.EXIT_ZERO,
            ),
            RunHostPayloadItemRequest(
                itemId = "payload-script-item",
                kind = RunHostPayloadKind.SCRIPT,
                required = false,
                artifactId = "payload-script",
                args = listOf("arg"),
                acceptance = RunHostPayloadAcceptance.EXIT_ZERO,
            ),
        ),
        finishItems = listOf(
            RunHostFinishItemRequest(
                itemId = "finish-script-item",
                kind = RunHostFinishKind.SCRIPT,
                severity = RunHostFinishSeverity.SAFETY_CRITICAL,
                alwaysRun = true,
                artifactId = "finish-script",
                args = listOf("--final"),
            ),
        ),
        artifacts = emptyList(),
    )

    /** Returns exact staged paths with immutable placeholder identities. */
    private fun artifacts(): Map<String, PublishedFlowArtifact> = mapOf(
        "profile" to artifact("profile", RunInputArtifactKind.PROFILE, "/workspace/profile"),
        "payload-script" to artifact(
            "payload-script",
            RunInputArtifactKind.USER_SCRIPT,
            "/workspace/script",
        ),
        "finish-script" to artifact(
            "finish-script",
            RunInputArtifactKind.USER_SCRIPT,
            "/workspace/finish",
        ),
    )

    /** Creates one pure staged artifact fixture. */
    private fun artifact(
        logicalId: String,
        kind: RunInputArtifactKind,
        path: String,
    ): PublishedFlowArtifact = PublishedFlowArtifact(logicalId, kind, path, ARTIFACT_IDENTITY)

    private companion object {
        val IDENTITY = RunHostIdentity("0123456789abcdef0123456789abcdef", 7L)
        val ARTIFACT_IDENTITY = PublishedArtifactIdentity(1uL, 1uL, 0L, "0".repeat(64), 0u)
    }
}
