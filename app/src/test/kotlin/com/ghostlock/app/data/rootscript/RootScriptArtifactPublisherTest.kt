package com.ghostlock.app.data.rootscript

import com.ghostlock.app.core.rootscript.ROOT_SCRIPT_HANDOFF_MARKER
import com.ghostlock.app.core.rootscript.RootScriptExecutionPlan
import com.ghostlock.app.core.rootscript.RootScriptPayloadAcceptance
import com.ghostlock.app.core.rootscript.RootScriptPayloadExecution
import com.ghostlock.app.core.rootscript.RootScriptPayloadKind
import com.ghostlock.app.core.runhost.RunHostIdentity
import java.io.File
import java.nio.file.Files
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

/** Physical publication tests for product and diagnostic root-script artifacts. */
@RunWith(RobolectricTestRunner::class)
class RootScriptArtifactPublisherTest {
    /** Product publication leaves one 0500 script, one 0600 result, and no sibling temporary. */
    @Test
    fun productPublicationUsesFixedModesAndFinalNames() {
        val workspace = Files.createTempDirectory("root-script-product").toFile()
        val artifact = RootScriptArtifactPublisher().publish(workspace, PLAN)

        assertEquals(0b111000000, mode(workspace.absolutePath))
        assertEquals(0b101000000, mode(artifact.scriptPath))
        assertEquals(0b110000000, mode(artifact.resultPath))
        assertTrue(artifact.scriptIdentity == PublishedArtifactAccess.capture(workspace.resolve("root-script.sh")))
        assertTrue(artifact.resultIdentity == PublishedArtifactAccess.capture(workspace.resolve("root-result.glr1")))
        assertTrue(workspace.resolve("root-script.sh").readText().contains(ROOT_SCRIPT_HANDOFF_MARKER))
        assertFalse(workspace.listFiles().orEmpty().any { file -> file.name.endsWith(".tmp") })
    }

    /** Diagnostic publication uses the fixed inert handoff body and the same final modes. */
    @Test
    fun diagnosticPublicationUsesFixedArtifact() {
        val workspace = Files.createTempDirectory("root-script-diagnostic").toFile()
        val artifact = RootScriptArtifactPublisher().publishDiagnostic(workspace, IDENTITY)

        val script = workspace.resolve("root-script.sh").readText(Charsets.US_ASCII)
        assertTrue(script.startsWith("#!/system/bin/sh\n$ROOT_SCRIPT_HANDOFF_MARKER\n"))
        assertTrue(script.contains("diagnose:shell:handoff-ok"))
        assertEquals(0b101000000, mode(artifact.scriptPath))
        assertEquals(0b110000000, mode(artifact.resultPath))
        assertTrue(workspace.resolve("diagnostic-result.empty").readBytes().isEmpty())
    }

    /** Existing targets and linked workspaces fail without replacing caller-owned entries. */
    @Test
    fun failureCleansOnlyPublisherOwnedPartialFiles() {
        val occupied = Files.createTempDirectory("root-script-occupied").toFile()
        val sentinel = occupied.resolve("root-script.sh").apply { writeText("sentinel") }

        assertThrows(IllegalStateException::class.java) {
            RootScriptArtifactPublisher().publish(occupied, PLAN)
        }

        assertEquals("sentinel", sentinel.readText())
        assertFalse(occupied.resolve("root-result.glr1").exists())
        assertFalse(occupied.listFiles().orEmpty().any { file -> file.name.endsWith(".tmp") })

        val parent = Files.createTempDirectory("root-script-link")
        val actual = Files.createDirectory(parent.resolve("actual"))
        val link = parent.resolve("link")
        Files.createSymbolicLink(link, actual.fileName)
        assertThrows(IllegalStateException::class.java) {
            RootScriptArtifactPublisher().publish(link.toFile(), PLAN)
        }
    }

    /** Returns only permission bits from one Android stat record. */
    private fun mode(path: String): Int =
        (Files.getAttribute(File(path).toPath(), "unix:mode") as Number).toInt() and 0b111111111

    private companion object {
        val IDENTITY = RunHostIdentity("0123456789abcdef0123456789abcdef", 1L)
        val PLAN = RootScriptExecutionPlan(
            runId = IDENTITY.runId,
            generation = IDENTITY.generation,
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
