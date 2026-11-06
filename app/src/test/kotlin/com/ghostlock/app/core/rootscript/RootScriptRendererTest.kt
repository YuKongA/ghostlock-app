package com.ghostlock.app.core.rootscript

import java.nio.file.Files
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

/** Golden-shape checks for the sole normalized Payload-then-Finish renderer. */
@RunWith(RobolectricTestRunner::class)
class RootScriptRendererTest {
    private val runId = "0123456789abcdef0123456789abcdef"

    @Test
    fun payloadAlwaysPrecedesFinishAndCompleteMarker() {
        val plan = basePlan(
            finish = listOf(
                RootScriptFinishExecution(
                    itemId = "finish-restore",
                    kind = RootScriptFinishKind.COMMAND,
                    severity = RootScriptFinishSeverity.SAFETY_CRITICAL,
                    alwaysRun = true,
                    stagedPath = null,
                    args = listOf("/system/bin/setenforce", "1"),
                ),
            ),
        )
        val script = RootScriptRenderer().renderText(plan, "/workspace/result.glr1")
        val payloadOffset = script.indexOf("# Payload payload-command")
        val finishOffset = script.indexOf("# Finish finish-restore")
        val completeOffset = script.indexOf("complete=1")
        assertTrue(payloadOffset in 0 until finishOffset)
        assertTrue(finishOffset in 0 until completeOffset)
        assertTrue(script.contains("PAYLOAD_BLOCKED=1"))
    }

    @Test
    fun rebootPublishesDurableHandoffBeforeRestart() {
        val plan = basePlan(
            finish = listOf(
                RootScriptFinishExecution(
                    itemId = "finish-reboot",
                    kind = RootScriptFinishKind.REBOOT,
                    severity = RootScriptFinishSeverity.SAFETY_CRITICAL,
                    alwaysRun = true,
                    stagedPath = null,
                    args = emptyList(),
                ),
            ),
        )
        val script = RootScriptRenderer().renderText(plan, "/workspace/result.glr1")
        assertTrue(script.indexOf("handoff=1") < script.indexOf("ctl.restart zygote"))
        assertTrue(script.indexOf("sync -f") < script.indexOf("ctl.restart zygote"))
    }

    @Test
    fun requiredFailFastAndFinishAlwaysRunRemainIndependent() {
        val secondPayload = commandPayload("payload-second", listOf("id"))
        val plan = basePlan(
            payload = listOf(commandPayload("payload-first", listOf("false")), secondPayload),
            finish = listOf(
                RootScriptFinishExecution(
                    "finish-normal",
                    RootScriptFinishKind.COMMAND,
                    RootScriptFinishSeverity.NORMAL,
                    false,
                    null,
                    listOf("/system/bin/id"),
                ),
                RootScriptFinishExecution(
                    "finish-always",
                    RootScriptFinishKind.COMMAND,
                    RootScriptFinishSeverity.SAFETY_CRITICAL,
                    true,
                    null,
                    listOf("/system/bin/setenforce", "1"),
                ),
            ),
        )
        val script = RootScriptRenderer().renderText(plan, "/workspace/result.glr1")
        assertTrue(script.indexOf("# Payload payload-first") < script.indexOf("# Payload payload-second"))
        assertTrue(script.contains("if [ \"\$PAYLOAD_BLOCKED\" -ne 0 ]; then"))
        assertTrue(script.contains("[ 0 -ne 1 ]"))
        assertTrue(script.contains("[ 1 -ne 1 ]"))
    }

    @Test
    fun rejectsAllOptionalDuplicateIdentityAndInvalidRebootShape() {
        val renderer = RootScriptRenderer()
        assertThrows(IllegalArgumentException::class.java) {
            renderer.renderText(
                basePlan(payload = listOf(commandPayload("payload-optional", listOf("id"), false))),
                "/workspace/result.glr1",
            )
        }
        assertThrows(IllegalArgumentException::class.java) {
            renderer.renderText(
                basePlan(
                    finish = listOf(
                        RootScriptFinishExecution(
                            "payload-command",
                            RootScriptFinishKind.COMMAND,
                            RootScriptFinishSeverity.NORMAL,
                            true,
                            null,
                            listOf("/system/bin/id"),
                        ),
                    ),
                ),
                "/workspace/result.glr1",
            )
        }
        assertThrows(IllegalArgumentException::class.java) {
            renderer.renderText(
                basePlan(
                    finish = listOf(
                        RootScriptFinishExecution(
                            "finish-reboot",
                            RootScriptFinishKind.REBOOT,
                            RootScriptFinishSeverity.SAFETY_CRITICAL,
                            false,
                            null,
                            emptyList(),
                        ),
                    ),
                ),
                "/workspace/result.glr1",
            )
        }
    }

    /** Rejects path replacement inputs and proves a successful publish leaves one complete final script. */
    @Test
    fun publishRejectsSymlinkWorkspaceAndExistingResultWithoutReplacement() {
        val renderer = RootScriptRenderer()
        val plan = basePlan()
        val parent = Files.createTempDirectory("root-script-publish")
        val actualWorkspace = Files.createDirectory(parent.resolve("actual"))
        val linkedWorkspace = parent.resolve("linked")
        Files.createSymbolicLink(linkedWorkspace, actualWorkspace.fileName)
        assertThrows(IllegalStateException::class.java) {
            renderer.publish(linkedWorkspace.toFile(), plan)
        }

        val occupiedWorkspace = Files.createDirectory(parent.resolve("occupied"))
        val result = occupiedWorkspace.resolve("root-result.glr1")
        Files.writeString(result, "sentinel")
        assertThrows(Exception::class.java) {
            renderer.publish(occupiedWorkspace.toFile(), plan)
        }
        assertEquals("sentinel", Files.readString(result))
        assertFalse(Files.exists(occupiedWorkspace.resolve("root-script.sh")))

        val occupiedScriptWorkspace = Files.createDirectory(parent.resolve("occupied-script"))
        val occupiedScript = occupiedScriptWorkspace.resolve("root-script.sh")
        Files.writeString(occupiedScript, "script-sentinel")
        assertThrows(Exception::class.java) {
            renderer.publish(occupiedScriptWorkspace.toFile(), plan)
        }
        assertEquals("script-sentinel", Files.readString(occupiedScript))
        assertFalse(Files.exists(occupiedScriptWorkspace.resolve("root-result.glr1")))

        val publishedWorkspace = Files.createDirectory(parent.resolve("published"))
        val artifact = renderer.publish(publishedWorkspace.toFile(), plan)
        val publishedScript = publishedWorkspace.resolve("root-script.sh")
        assertTrue(artifact.scriptIdentity.matches(publishedScript.toFile()))
        assertTrue(Files.readString(publishedScript).startsWith("#!/system/bin/sh\n"))
        Files.list(publishedWorkspace).use { entries ->
            assertFalse(entries.anyMatch { path -> path.fileName.toString().endsWith(".tmp") })
        }
    }

    private fun basePlan(
        payload: List<RootScriptPayloadExecution> = listOf(commandPayload("payload-command", listOf("id"))),
        finish: List<RootScriptFinishExecution> = emptyList(),
    ): RootScriptExecutionPlan = RootScriptExecutionPlan(runId, 1L, payload, finish)

    private fun commandPayload(
        itemId: String,
        args: List<String>,
        required: Boolean = true,
    ): RootScriptPayloadExecution = RootScriptPayloadExecution(
        itemId = itemId,
        kind = RootScriptPayloadKind.COMMAND,
        required = required,
        stagedPath = null,
        args = args,
        acceptance = RootScriptPayloadAcceptance.EXIT_ZERO,
    )
}
