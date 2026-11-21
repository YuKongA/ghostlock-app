package com.ghostlock.app.data.rootscript

import com.ghostlock.app.core.flow.RootScriptDecodeOutcome
import com.ghostlock.app.core.rootscript.RootScriptExecutionPlan
import com.ghostlock.app.core.rootscript.RootScriptPayloadAcceptance
import com.ghostlock.app.core.rootscript.RootScriptPayloadExecution
import com.ghostlock.app.core.rootscript.RootScriptPayloadKind
import com.ghostlock.app.core.rootscript.RootScriptResultCodec
import java.io.File
import java.nio.file.Files
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

/** Total-function tests for same-object bounded Android GLR1 result reading. */
@RunWith(RobolectricTestRunner::class)
class AndroidRootScriptResultReaderTest {
    /** A valid same-object document decodes to the existing complete outcome. */
    @Test
    fun decodesValidSameObjectResult() {
        val artifact = publish()
        File(artifact.resultPath).writeText(validResult(), Charsets.US_ASCII)

        val outcome = AndroidRootScriptResultReader(artifact, PLAN).read()

        assertTrue(outcome is RootScriptDecodeOutcome.Complete)
        assertEquals(RUN_ID, (outcome as RootScriptDecodeOutcome.Complete).result.runId)
    }

    /** Missing, linked, or replaced paths map to identity mismatch without escaping. */
    @Test
    fun physicalIdentityFailuresNeverEscape() {
        val missing = publish()
        File(missing.resultPath).delete()
        assertTrue(
            AndroidRootScriptResultReader(missing, PLAN).read() ===
                RootScriptDecodeOutcome.IdentityMismatch,
        )

        val replaced = publish()
        val result = File(replaced.resultPath)
        result.delete()
        result.writeText(validResult(), Charsets.US_ASCII)
        assertTrue(
            AndroidRootScriptResultReader(replaced, PLAN).read() ===
                RootScriptDecodeOutcome.IdentityMismatch,
        )

        val linked = publish()
        val link = File(linked.resultPath).toPath()
        Files.delete(link)
        val target = Files.writeString(link.parent.resolve("replacement.glr1"), validResult())
        Files.createSymbolicLink(link, target.fileName)
        assertTrue(
            AndroidRootScriptResultReader(linked, PLAN).read() ===
                RootScriptDecodeOutcome.IdentityMismatch,
        )
    }

    /** Oversize, truncated, and grammar-invalid same-object bytes all map to malformed. */
    @Test
    fun boundedReadAndCodecFailuresNeverEscape() {
        val cases = listOf(
            ByteArray(RootScriptResultCodec.MAX_RESULT_BYTES + 1) { 'a'.code.toByte() },
            "GLR1\n".toByteArray(Charsets.US_ASCII),
            validResult().replace("complete=1", "complete=0").toByteArray(Charsets.US_ASCII),
        )
        cases.forEach { bytes ->
            val artifact = publish()
            File(artifact.resultPath).writeBytes(bytes)
            assertTrue(
                AndroidRootScriptResultReader(artifact, PLAN).read() ===
                    RootScriptDecodeOutcome.Malformed,
            )
        }
    }

    /** Publishes one fresh product object pair through the production physical owner. */
    private fun publish() = RootScriptArtifactPublisher().publish(
        Files.createTempDirectory("root-result-reader").toFile(),
        PLAN,
    )

    /** Returns the exact valid GLR1 document for [PLAN]. */
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
