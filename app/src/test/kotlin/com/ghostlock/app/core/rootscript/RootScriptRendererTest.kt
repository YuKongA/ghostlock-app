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

    /** Renders every normalized Payload and Finish kind into one deterministic root script. */
    @Test
    fun allPayloadAndFinishKindsRenderExactGoldenScript() {
        val plan = RootScriptExecutionPlan(
            runId = runId,
            generation = 7L,
            payload = listOf(
                RootScriptPayloadExecution(
                    itemId = "payload-command",
                    kind = RootScriptPayloadKind.COMMAND,
                    required = true,
                    stagedPath = null,
                    args = listOf("/system/bin/id", "--context"),
                    acceptance = RootScriptPayloadAcceptance.EXIT_ZERO,
                ),
                RootScriptPayloadExecution(
                    itemId = "payload-script",
                    kind = RootScriptPayloadKind.SCRIPT,
                    required = true,
                    stagedPath = "/workspace/payload-user.sh",
                    args = listOf("--payload-arg"),
                    acceptance = RootScriptPayloadAcceptance.EXIT_ZERO,
                ),
                RootScriptPayloadExecution(
                    itemId = "payload-root-manager",
                    kind = RootScriptPayloadKind.ROOT_MANAGER,
                    required = true,
                    stagedPath = "/workspace/libksud.so",
                    args = listOf("late-load", "--allow-shell", "--kmi", "android15-6.6"),
                    acceptance = RootScriptPayloadAcceptance.DAEMON_ACK,
                ),
                RootScriptPayloadExecution(
                    itemId = "payload-lkm",
                    kind = RootScriptPayloadKind.LKM,
                    required = true,
                    stagedPath = "/workspace/ghostlock_test.ko",
                    args = listOf("mode=1"),
                    acceptance = RootScriptPayloadAcceptance.MODULE_PRESENT,
                ),
                RootScriptPayloadExecution(
                    itemId = "payload-detached",
                    kind = RootScriptPayloadKind.DETACHED_PROCESS,
                    required = true,
                    stagedPath = "/workspace/root-daemon",
                    args = listOf("--takeover"),
                    acceptance = RootScriptPayloadAcceptance.TAKEOVER_ACK,
                ),
            ),
            finish = listOf(
                RootScriptFinishExecution(
                    itemId = "finish-restore-selinux",
                    kind = RootScriptFinishKind.COMMAND,
                    severity = RootScriptFinishSeverity.SAFETY_CRITICAL,
                    alwaysRun = true,
                    stagedPath = null,
                    args = listOf("/system/bin/setenforce", "1"),
                ),
                RootScriptFinishExecution(
                    itemId = "finish-script",
                    kind = RootScriptFinishKind.SCRIPT,
                    severity = RootScriptFinishSeverity.NORMAL,
                    alwaysRun = true,
                    stagedPath = "/workspace/finish-user.sh",
                    args = listOf("--finish-arg"),
                ),
                RootScriptFinishExecution(
                    itemId = "finish-soft-reboot",
                    kind = RootScriptFinishKind.REBOOT,
                    severity = RootScriptFinishSeverity.SAFETY_CRITICAL,
                    alwaysRun = true,
                    stagedPath = null,
                    args = emptyList(),
                ),
            ),
        )

        val script = RootScriptRenderer().renderText(plan, "/workspace/root-result.glr1")
        val expected = """
            #!/system/bin/sh
            # GHOSTLOCK_HANDOFF_V1
            set -u
            RESULT='/workspace/root-result.glr1'
            exec 9>"${'$'}RESULT"
            write_item() {
              printf '%s.%s.state=%s\n' "${'$'}1" "${'$'}2" "${'$'}3" >&9
              printf '%s.%s.reason=%s\n' "${'$'}1" "${'$'}2" "${'$'}4" >&9
              printf '%s.%s.exit_kind=%s\n' "${'$'}1" "${'$'}2" "${'$'}5" >&9
              printf '%s.%s.exit_value=%s\n' "${'$'}1" "${'$'}2" "${'$'}6" >&9
            }
            printf 'GLR1\nrun=%s\ngeneration=%s\n' '0123456789abcdef0123456789abcdef' 7 >&9
            PAYLOAD_REQUIRED_OK=1
            PAYLOAD_BLOCKED=0
            # Payload payload-command
            if [ "${'$'}PAYLOAD_BLOCKED" -ne 0 ]; then
              write_item payload 'payload-command' 4 1 0 0
            else
              ITEM_EXIT=0
              '/system/bin/id' '--context'
              ITEM_EXIT=${'$'}?
              if [ "${'$'}ITEM_EXIT" -ne 0 ]; then
                write_item payload 'payload-command' 3 5 1 "${'$'}ITEM_EXIT"
                PAYLOAD_REQUIRED_OK=0
                PAYLOAD_BLOCKED=1
              elif [ "${'$'}ITEM_EXIT" -eq 0 ]; then
                write_item payload 'payload-command' 2 0 1 0
              else
                write_item payload 'payload-command' 5 14 0 0
                PAYLOAD_REQUIRED_OK=0
                PAYLOAD_BLOCKED=1
              fi
            fi
            # Payload payload-script
            if [ "${'$'}PAYLOAD_BLOCKED" -ne 0 ]; then
              write_item payload 'payload-script' 4 1 0 0
            else
              ITEM_EXIT=0
              '/system/bin/sh' '/workspace/payload-user.sh' '--payload-arg'
              ITEM_EXIT=${'$'}?
              if [ "${'$'}ITEM_EXIT" -ne 0 ]; then
                write_item payload 'payload-script' 3 5 1 "${'$'}ITEM_EXIT"
                PAYLOAD_REQUIRED_OK=0
                PAYLOAD_BLOCKED=1
              elif [ "${'$'}ITEM_EXIT" -eq 0 ]; then
                write_item payload 'payload-script' 2 0 1 0
              else
                write_item payload 'payload-script' 5 14 0 0
                PAYLOAD_REQUIRED_OK=0
                PAYLOAD_BLOCKED=1
              fi
            fi
            # Payload payload-root-manager
            if [ "${'$'}PAYLOAD_BLOCKED" -ne 0 ]; then
              write_item payload 'payload-root-manager' 4 1 0 0
            else
              ITEM_EXIT=0
              '/workspace/libksud.so' 'late-load' '--allow-shell' '--kmi' 'android15-6.6'
              ITEM_EXIT=${'$'}?
              if [ "${'$'}ITEM_EXIT" -ne 0 ]; then
                write_item payload 'payload-root-manager' 3 5 1 "${'$'}ITEM_EXIT"
                PAYLOAD_REQUIRED_OK=0
                PAYLOAD_BLOCKED=1
              elif command -v su >/dev/null 2>&1 || [ -d /sys/module/kernelsu ]; then
                write_item payload 'payload-root-manager' 2 0 1 0
              else
                write_item payload 'payload-root-manager' 5 14 0 0
                PAYLOAD_REQUIRED_OK=0
                PAYLOAD_BLOCKED=1
              fi
            fi
            # Payload payload-lkm
            if [ "${'$'}PAYLOAD_BLOCKED" -ne 0 ]; then
              write_item payload 'payload-lkm' 4 1 0 0
            else
              ITEM_EXIT=0
              '/system/bin/insmod' '/workspace/ghostlock_test.ko' 'mode=1'
              ITEM_EXIT=${'$'}?
              if [ "${'$'}ITEM_EXIT" -ne 0 ]; then
                write_item payload 'payload-lkm' 3 5 1 "${'$'}ITEM_EXIT"
                PAYLOAD_REQUIRED_OK=0
                PAYLOAD_BLOCKED=1
              elif [ -d /sys/module/ghostlock_test ] || grep -q '^ghostlock_test ' /proc/modules 2>/dev/null; then
                write_item payload 'payload-lkm' 2 0 1 0
              else
                write_item payload 'payload-lkm' 5 14 0 0
                PAYLOAD_REQUIRED_OK=0
                PAYLOAD_BLOCKED=1
              fi
            fi
            # Payload payload-detached
            if [ "${'$'}PAYLOAD_BLOCKED" -ne 0 ]; then
              write_item payload 'payload-detached' 4 1 0 0
            else
              ITEM_EXIT=0
              '/workspace/root-daemon' '--takeover' >/dev/null 2>&1 &
              ITEM_EXIT=${'$'}?
              if [ "${'$'}ITEM_EXIT" -ne 0 ]; then
                write_item payload 'payload-detached' 3 5 1 "${'$'}ITEM_EXIT"
                PAYLOAD_REQUIRED_OK=0
                PAYLOAD_BLOCKED=1
              elif [ "${'$'}ITEM_EXIT" -eq 0 ]; then
                write_item payload 'payload-detached' 2 0 1 0
              else
                write_item payload 'payload-detached' 5 14 0 0
                PAYLOAD_REQUIRED_OK=0
                PAYLOAD_BLOCKED=1
              fi
            fi
            # Finish finish-restore-selinux
            if [ "${'$'}PAYLOAD_REQUIRED_OK" -ne 1 ] && [ 1 -ne 1 ]; then
              write_item finish 'finish-restore-selinux' 4 1 0 0
            else
              '/system/bin/setenforce' '1'
              ITEM_EXIT=${'$'}?
              if [ "${'$'}ITEM_EXIT" -eq 0 ]; then
                write_item finish 'finish-restore-selinux' 2 0 1 0
              else
                write_item finish 'finish-restore-selinux' 3 5 1 "${'$'}ITEM_EXIT"
              fi
            fi
            # Finish finish-script
            if [ "${'$'}PAYLOAD_REQUIRED_OK" -ne 1 ] && [ 1 -ne 1 ]; then
              write_item finish 'finish-script' 4 1 0 0
            else
              '/system/bin/sh' '/workspace/finish-user.sh' '--finish-arg'
              ITEM_EXIT=${'$'}?
              if [ "${'$'}ITEM_EXIT" -eq 0 ]; then
                write_item finish 'finish-script' 2 0 1 0
              else
                write_item finish 'finish-script' 3 5 1 "${'$'}ITEM_EXIT"
              fi
            fi
            # Finish finish-soft-reboot
            if [ "${'$'}PAYLOAD_REQUIRED_OK" -ne 1 ] && [ 1 -ne 1 ]; then
              write_item finish 'finish-soft-reboot' 4 1 0 0
            else
              write_item finish 'finish-soft-reboot' 1 0 0 0
              printf 'handoff=1\ncomplete=1\n' >&9
              sync -f /proc/self/fd/9 2>/dev/null || sync
              /system/bin/setprop ctl.restart zygote
              exit 0
            fi
            printf 'handoff=0\ncomplete=1\n' >&9
            sync -f /proc/self/fd/9 2>/dev/null || sync
            exit 0
        """.trimIndent() + "\n"
        assertEquals(expected, script)
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
        assertTrue(
            Files.readString(publishedScript).startsWith(
                "#!/system/bin/sh\n# GHOSTLOCK_HANDOFF_V1\n",
            ),
        )
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
