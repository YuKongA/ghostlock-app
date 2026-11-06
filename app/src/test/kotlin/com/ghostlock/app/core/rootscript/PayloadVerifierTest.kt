package com.ghostlock.app.core.rootscript

import org.junit.Assert.assertTrue
import org.junit.Test

/** Mechanical checks for normalized Payload acceptance evidence. */
class PayloadVerifierTest {
    @Test
    fun rootManagerRequiresDaemonEvidenceWithoutCatalogLookup() {
        val condition = PayloadVerifier.condition(
            RootScriptPayloadExecution(
                itemId = "payload-root",
                kind = RootScriptPayloadKind.ROOT_MANAGER,
                required = true,
                stagedPath = "/workspace/manager.exec",
                args = listOf("late-load"),
                acceptance = RootScriptPayloadAcceptance.DAEMON_ACK,
            ),
        )
        assertTrue(condition.contains("command -v su"))
        assertTrue(condition.contains("/sys/module/kernelsu"))
    }

    @Test
    fun lkmUsesStagedModuleIdentityInsteadOfLogText() {
        val condition = PayloadVerifier.condition(
            RootScriptPayloadExecution(
                itemId = "payload-lkm",
                kind = RootScriptPayloadKind.LKM,
                required = true,
                stagedPath = "/workspace/module.ko",
                args = emptyList(),
                acceptance = RootScriptPayloadAcceptance.MODULE_PRESENT,
            ),
        )
        assertTrue(condition.contains("/sys/module/module"))
        assertTrue(condition.contains("/proc/modules"))
    }
}
