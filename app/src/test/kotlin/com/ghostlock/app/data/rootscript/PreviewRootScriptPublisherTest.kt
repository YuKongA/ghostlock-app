package com.ghostlock.app.data.rootscript

import com.ghostlock.app.core.rootscript.ROOT_SCRIPT_HANDOFF_MARKER
import com.ghostlock.app.domain.payload.PayloadAcceptance
import com.ghostlock.app.domain.payload.PayloadDefinition
import com.ghostlock.app.domain.payload.PayloadKind
import com.ghostlock.app.domain.payload.PayloadSlot
import com.ghostlock.app.domain.run.PreviewRootScriptUseCase
import java.io.File
import java.nio.file.Files
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

/** Payload-only preview tests through the production physical publisher. */
@RunWith(RobolectricTestRunner::class)
class PreviewRootScriptPublisherTest {
    /** The preview use case injects empty Finish and publishes only the fixed 0500 handoff script. */
    @Test
    fun payloadOnlyPreviewUsesProductionArtifactWithoutDispatch() {
        val previewRoot = Files.createTempDirectory("root-script-preview").toFile()
        val useCase = PreviewRootScriptUseCase(
            PreviewRootScriptPublisher(previewRoot, RootScriptArtifactPublisher()),
        )
        val path = useCase(
            PayloadDefinition(
                items = listOf(
                    PayloadSlot(
                        id = "payload-command",
                        kind = PayloadKind.Command,
                        acceptance = PayloadAcceptance.ExitZero,
                        command = "echo preview",
                    ),
                ),
            ),
        )
        val script = File(path)
        val text = script.readText()

        assertEquals("root-script.sh", script.name)
        val scriptMode = (Files.getAttribute(script.toPath(), "unix:mode") as Number).toInt()
        assertEquals(0b101000000, scriptMode and 0b111111111)
        assertTrue(text.startsWith("#!/system/bin/sh\n$ROOT_SCRIPT_HANDOFF_MARKER\n"))
        assertTrue(text.contains("# Payload payload-command"))
        assertFalse(text.contains("# Finish "))
        assertTrue(requireNotNull(script.parentFile).resolve("root-result.glr1").isFile)
        assertEquals(1, previewRoot.listFiles().orEmpty().size)
    }
}
