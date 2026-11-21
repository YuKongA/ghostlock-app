package com.ghostlock.app.data.rootscript

import android.system.Os
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.ghostlock.app.core.flow.RootScriptDecodeOutcome
import com.ghostlock.app.core.rootscript.RootScriptExecutionPlan
import com.ghostlock.app.core.rootscript.RootScriptPayloadAcceptance
import com.ghostlock.app.core.rootscript.RootScriptPayloadExecution
import com.ghostlock.app.core.rootscript.RootScriptPayloadKind
import com.ghostlock.app.core.rootscript.RootScriptResultCodec
import java.io.File
import java.nio.file.Files
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/** Device-filesystem gate for no-follow publication, modes, identity, and bounded result reading. */
@RunWith(AndroidJUnit4::class)
class RootScriptPhysicalIoTest {
    /** Product publication and same-object read use the real Android filesystem contract. */
    @Test
    fun publishAndReadSameObjectOnAndroidFilesystem() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val workspace = File(context.cacheDir, "root-physical-${System.nanoTime()}")
        val artifact = RootScriptArtifactPublisher().publish(workspace, PLAN)
        try {
            assertEquals(0b111000000, mode(workspace))
            assertEquals(0b101000000, mode(File(artifact.scriptPath)))
            assertEquals(0b110000000, mode(File(artifact.resultPath)))
            assertFalse(workspace.listFiles().orEmpty().any { file -> file.name.endsWith(".tmp") })

            File(artifact.resultPath).writeText(validResult(), Charsets.US_ASCII)
            assertTrue(
                AndroidRootScriptResultReader(artifact, PLAN).read() is
                    RootScriptDecodeOutcome.Complete,
            )

            File(artifact.resultPath).writeBytes(
                ByteArray(RootScriptResultCodec.MAX_RESULT_BYTES + 1) { 'x'.code.toByte() },
            )
            assertTrue(
                AndroidRootScriptResultReader(artifact, PLAN).read() ===
                    RootScriptDecodeOutcome.Malformed,
            )
        } finally {
            workspace.deleteRecursively()
        }
    }

    /** Replacing the result inode or substituting a symlink maps to identity mismatch. */
    @Test
    fun replacementAndSymlinkCannotReuseCapturedIdentity() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val replacementWorkspace = File(context.cacheDir, "root-replace-${System.nanoTime()}")
        val replacementArtifact = RootScriptArtifactPublisher().publish(replacementWorkspace, PLAN)
        try {
            val result = File(replacementArtifact.resultPath)
            assertTrue(result.delete())
            result.writeText(validResult(), Charsets.US_ASCII)
            assertTrue(
                AndroidRootScriptResultReader(replacementArtifact, PLAN).read() ===
                    RootScriptDecodeOutcome.IdentityMismatch,
            )
        } finally {
            replacementWorkspace.deleteRecursively()
        }

        val linkWorkspace = File(context.cacheDir, "root-link-${System.nanoTime()}")
        val linkArtifact = RootScriptArtifactPublisher().publish(linkWorkspace, PLAN)
        try {
            val path = File(linkArtifact.resultPath).toPath()
            Files.delete(path)
            val target = path.parent.resolve("target.glr1")
            target.toFile().writeText(validResult(), Charsets.US_ASCII)
            Files.createSymbolicLink(path, target.fileName)
            assertTrue(
                AndroidRootScriptResultReader(linkArtifact, PLAN).read() ===
                    RootScriptDecodeOutcome.IdentityMismatch,
            )
        } finally {
            linkWorkspace.deleteRecursively()
        }
    }

    /** Returns exact permission bits from the Android stat result. */
    private fun mode(file: File): Int = Os.stat(file.absolutePath).st_mode and 0b111111111

    /** Returns one valid result for [PLAN]. */
    private fun validResult(): String =
        "GLR1\n" +
            "run=$RUN_ID\n" +
            "generation=1\n" +
            "payload.payload-command.state=2\n" +
            "payload.payload-command.reason=0\n" +
            "payload.payload-command.exit_kind=1\n" +
            "payload.payload-command.exit_value=0\n" +
            "handoff=0\n" +
            "complete=1\n"

    private companion object {
        const val RUN_ID = "0123456789abcdef0123456789abcdef"
        val PLAN = RootScriptExecutionPlan(
            RUN_ID,
            1L,
            listOf(
                RootScriptPayloadExecution(
                    "payload-command",
                    RootScriptPayloadKind.COMMAND,
                    true,
                    null,
                    listOf("id"),
                    RootScriptPayloadAcceptance.EXIT_ZERO,
                ),
            ),
            emptyList(),
        )
    }
}
