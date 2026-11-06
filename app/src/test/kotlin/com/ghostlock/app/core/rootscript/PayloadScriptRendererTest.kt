package com.ghostlock.app.core.rootscript

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** Normalized Payload fragment checks beside the sole renderer. */
class PayloadScriptRendererTest {
    @Test
    fun directCommandUsesQuotedArgvWithoutShellExpansion() {
        val action = PayloadScriptRenderer.render(
            RootScriptPayloadExecution(
                itemId = "payload-command",
                kind = RootScriptPayloadKind.COMMAND,
                required = true,
                stagedPath = null,
                args = listOf("echo", "hello world"),
                acceptance = RootScriptPayloadAcceptance.EXIT_ZERO,
            ),
        )
        assertEquals("'echo' 'hello world'", action.lines.first())
        assertEquals("[ \"\$ITEM_EXIT\" -eq 0 ]", action.acceptanceCondition)
    }

    @Test
    fun normalizedStagedItemsNeverSearchPackageOrFallbackDirectories() {
        val manager = PayloadScriptRenderer.render(
            RootScriptPayloadExecution(
                itemId = "payload-root",
                kind = RootScriptPayloadKind.ROOT_MANAGER,
                required = true,
                stagedPath = "/workspace/manager.exec",
                args = listOf("late-load", "--kmi", "android13-5.15"),
                acceptance = RootScriptPayloadAcceptance.DAEMON_ACK,
            ),
        )
        val text = manager.lines.joinToString("\n")
        assertTrue(text.contains("'/workspace/manager.exec' 'late-load' '--kmi' 'android13-5.15'"))
        assertFalse(text.contains("find /data/app"))
        assertFalse(text.contains("/data/adb"))

        val lkm = PayloadScriptRenderer.render(
            RootScriptPayloadExecution(
                itemId = "payload-lkm",
                kind = RootScriptPayloadKind.LKM,
                required = false,
                stagedPath = "/workspace/module.ko",
                args = listOf("mode=1"),
                acceptance = RootScriptPayloadAcceptance.MODULE_PRESENT,
            ),
        )
        assertTrue(lkm.lines.first().contains("'/workspace/module.ko' 'mode=1'"))
    }
}
