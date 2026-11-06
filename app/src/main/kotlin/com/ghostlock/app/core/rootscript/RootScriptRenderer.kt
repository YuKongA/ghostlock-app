package com.ghostlock.app.core.rootscript

import android.system.Os
import android.system.OsConstants
import com.ghostlock.app.domain.finish.FinishDefinition
import com.ghostlock.app.domain.finish.FinishKind
import com.ghostlock.app.domain.payload.PayloadAcceptance
import com.ghostlock.app.domain.payload.PayloadDefinition
import com.ghostlock.app.domain.payload.PayloadKind
import com.ghostlock.app.domain.payload.PayloadRootManagers
import com.ghostlock.app.domain.run.RootScriptPreviewPort
import java.io.File
import java.io.FileOutputStream
import java.nio.charset.StandardCharsets
import java.nio.file.Files
import java.nio.file.LinkOption
import java.nio.file.StandardCopyOption
import java.util.UUID

/** Produces and publishes the sole normalized Payload-then-Finish root script artifact. */
class RootScriptRenderer {
    /**
     * Renders deterministic script text for one pre-created result path.
     *
     * @param plan Fully resolved execution plan; no lookup remains below this boundary.
     * @param resultPath Pre-created result path in the RunHost namespace.
     * @return LF-only script ending with a newline.
     */
    fun renderText(plan: RootScriptExecutionPlan, resultPath: String): String {
        plan.validate()
        return buildString {
            appendLine("#!/system/bin/sh")
            appendLine("set -u")
            appendLine("RESULT=${PayloadScriptRenderer.shellQuote(resultPath)}")
            appendLine("exec 9>\"\$RESULT\"")
            appendLine("write_item() {")
            appendLine("  printf '%s.%s.state=%s\\n' \"\$1\" \"\$2\" \"\$3\" >&9")
            appendLine("  printf '%s.%s.reason=%s\\n' \"\$1\" \"\$2\" \"\$4\" >&9")
            appendLine("  printf '%s.%s.exit_kind=%s\\n' \"\$1\" \"\$2\" \"\$5\" >&9")
            appendLine("  printf '%s.%s.exit_value=%s\\n' \"\$1\" \"\$2\" \"\$6\" >&9")
            appendLine("}")
            appendLine(
                "printf 'GLR1\\nrun=%s\\ngeneration=%s\\n' " +
                    PayloadScriptRenderer.shellQuote(plan.runId) + " " + plan.generation + " >&9",
            )
            appendLine("PAYLOAD_REQUIRED_OK=1")
            appendLine("PAYLOAD_BLOCKED=0")
            plan.payload.forEach { item ->
                appendLine("# Payload ${item.itemId}")
                appendLine("if [ \"\$PAYLOAD_BLOCKED\" -ne 0 ]; then")
                appendLine(
                    "  write_item payload ${PayloadScriptRenderer.shellQuote(item.itemId)} 4 1 0 0",
                )
                appendLine("else")
                appendLine("  ITEM_EXIT=0")
                val action = PayloadScriptRenderer.render(item)
                action.lines.forEach { line -> appendLine("  $line") }
                appendLine("  if [ \"\$ITEM_EXIT\" -ne 0 ]; then")
                appendLine(
                    "    write_item payload ${PayloadScriptRenderer.shellQuote(item.itemId)} 3 5 1 \"\$ITEM_EXIT\"",
                )
                if (item.required) {
                    appendLine("    PAYLOAD_REQUIRED_OK=0")
                    appendLine("    PAYLOAD_BLOCKED=1")
                }
                appendLine("  elif ${action.acceptanceCondition}; then")
                appendLine(
                    "    write_item payload ${PayloadScriptRenderer.shellQuote(item.itemId)} 2 0 1 0",
                )
                appendLine("  else")
                appendLine(
                    "    write_item payload ${PayloadScriptRenderer.shellQuote(item.itemId)} 5 14 0 0",
                )
                if (item.required) {
                    appendLine("    PAYLOAD_REQUIRED_OK=0")
                    appendLine("    PAYLOAD_BLOCKED=1")
                }
                appendLine("  fi")
                appendLine("fi")
            }
            plan.finish.forEach { item ->
                appendLine("# Finish ${item.itemId}")
                appendLine(
                    "if [ \"\$PAYLOAD_REQUIRED_OK\" -ne 1 ] && " +
                        "[ ${if (item.alwaysRun) 1 else 0} -ne 1 ]; then",
                )
                appendLine(
                    "  write_item finish ${PayloadScriptRenderer.shellQuote(item.itemId)} 4 1 0 0",
                )
                appendLine("else")
                when (item.kind) {
                    RootScriptFinishKind.COMMAND -> appendFinishProcess(item.itemId, item.args)
                    RootScriptFinishKind.SCRIPT -> appendFinishProcess(
                        item.itemId,
                        listOf("/system/bin/sh", requireNotNull(item.stagedPath)) + item.args,
                    )
                    RootScriptFinishKind.REBOOT -> {
                        appendLine(
                            "  write_item finish ${PayloadScriptRenderer.shellQuote(item.itemId)} 1 0 0 0",
                        )
                        appendLine("  printf 'handoff=1\\ncomplete=1\\n' >&9")
                        appendLine("  sync -f /proc/self/fd/9 2>/dev/null || sync")
                        appendLine("  /system/bin/setprop ctl.restart zygote")
                        appendLine("  exit 0")
                    }
                }
                appendLine("fi")
            }
            appendLine("printf 'handoff=0\\ncomplete=1\\n' >&9")
            appendLine("sync -f /proc/self/fd/9 2>/dev/null || sync")
            appendLine("exit 0")
        }
    }

    /**
     * Writes, fsyncs, permission-locks, and publishes a script plus pre-created result object.
     *
     * @param workspace Fresh RunHost-owned root-script directory.
     * @param plan Fully normalized run input.
     * @return Published artifact contract; no backend execution occurs.
     */
    fun publish(workspace: File, plan: RootScriptExecutionPlan): RootScriptArtifact {
        plan.validate()
        check(workspace.isDirectory || workspace.mkdirs()) { "cannot create root-script workspace" }
        check(!Files.isSymbolicLink(workspace.toPath())) { "workspace must not be a symlink" }
        Os.chmod(workspace.absolutePath, 0b111000000)
        val result = File(workspace, "root-result.glr1")
        check(!Files.exists(result.toPath(), LinkOption.NOFOLLOW_LINKS)) { "root result already exists" }
        val script = File(workspace, "root-script.sh")
        val temporary = File(workspace, ".root-script.${UUID.randomUUID()}.tmp")
        var ownsResult = false
        var ownsTemporary = false
        var ownsScript = false
        try {
            createExclusiveEmptyFile(result, 0b110000000)
            ownsResult = true
            val resultIdentity = PublishedArtifactIdentity.capture(result)
            writeExclusive(temporary, renderText(plan, result.absolutePath), 0b110000000)
            ownsTemporary = true
            Os.chmod(temporary.absolutePath, 0b101000000)
            publishExclusiveMove(temporary, script)
            ownsTemporary = false
            ownsScript = true
            return RootScriptArtifact(
                runId = plan.runId,
                generation = plan.generation,
                scriptPath = script.absolutePath,
                resultPath = result.absolutePath,
                scriptIdentity = PublishedArtifactIdentity.capture(script),
                resultIdentity = resultIdentity,
                retainedArtifacts = plan.retainedArtifacts.toList(),
            )
        } catch (error: Exception) {
            if (ownsTemporary) temporary.delete()
            if (ownsScript) script.delete()
            if (ownsResult) result.delete()
            throw IllegalStateException("cannot publish root script", error)
        }
    }

    /**
     * Publishes one completed sibling temporary through an atomic same-directory move.
     *
     * @param temporary Exclusive, fsynced, permission-locked source file.
     * @param script Final path inside the same fresh owner-only workspace.
     * @return Unit after the final path names the same inode previously held by [temporary].
     * @throws IllegalStateException When the destination already exists.
     * @note The 0700 fresh workspace excludes another application writer; Android SELinux permits rename but
     * denies hard-link creation for `untrusted_app` app-data files.
     */
    private fun publishExclusiveMove(temporary: File, script: File) {
        check(!Files.exists(script.toPath(), LinkOption.NOFOLLOW_LINKS)) {
            "root script already exists"
        }
        Files.move(temporary.toPath(), script.toPath(), StandardCopyOption.ATOMIC_MOVE)
    }

    /** Appends one direct process action and its strict exit fact. */
    private fun StringBuilder.appendFinishProcess(itemId: String, argv: List<String>) {
        require(argv.isNotEmpty() && argv.size <= 32) { "invalid Finish argv" }
        appendLine("  ${argv.joinToString(separator = " ") { PayloadScriptRenderer.shellQuote(it) }}")
        appendLine("  ITEM_EXIT=\$?")
        appendLine("  if [ \"\$ITEM_EXIT\" -eq 0 ]; then")
        appendLine("    write_item finish ${PayloadScriptRenderer.shellQuote(itemId)} 2 0 1 0")
        appendLine("  else")
        appendLine(
            "    write_item finish ${PayloadScriptRenderer.shellQuote(itemId)} 3 5 1 \"\$ITEM_EXIT\"",
        )
        appendLine("  fi")
    }

    /** Writes one new file through `O_EXCL|O_NOFOLLOW` and fsyncs before close. */
    private fun writeExclusive(file: File, text: String, mode: Int) {
        val descriptor = Os.open(
            file.absolutePath,
            OsConstants.O_CREAT or OsConstants.O_EXCL or OsConstants.O_WRONLY or
                OsConstants.O_CLOEXEC or OsConstants.O_NOFOLLOW,
            mode,
        )
        try {
            FileOutputStream(descriptor).use { output ->
                output.write(text.toByteArray(StandardCharsets.UTF_8))
                output.flush()
                output.fd.sync()
            }
        } catch (error: Exception) {
            file.delete()
            throw error
        }
    }

    /** Creates and fsyncs one new empty regular file without following links. */
    private fun createExclusiveEmptyFile(file: File, mode: Int) {
        val descriptor = Os.open(
            file.absolutePath,
            OsConstants.O_CREAT or OsConstants.O_EXCL or OsConstants.O_WRONLY or
                OsConstants.O_CLOEXEC or OsConstants.O_NOFOLLOW,
            mode,
        )
        try {
            FileOutputStream(descriptor).use { output -> output.fd.sync() }
        } catch (error: Exception) {
            file.delete()
            throw error
        }
    }
}

/**
 * Preview adapter that converts documents to the same normalized renderer input.
 *
 * @param previewRoot App-private parent dedicated to non-executable previews.
 * @param renderer Sole production renderer implementation.
 */
class PreviewRootScriptPublisher(
    private val previewRoot: File,
    private val renderer: RootScriptRenderer,
) : RootScriptPreviewPort {
    /** @return Absolute path of a newly published preview script. */
    override fun preview(payload: PayloadDefinition, finish: FinishDefinition): String {
        check(previewRoot.isDirectory || previewRoot.mkdirs()) { "cannot create preview root" }
        val previewId = UUID.randomUUID().toString().replace("-", "")
        val workspace = File(previewRoot, previewId)
        val inputs = File(workspace, "inputs")
        val payloadPlan = payload.enabledItems().map { item ->
            val stagedPath = File(inputs, item.id).absolutePath.takeIf {
                item.kind != PayloadKind.Command || item.scriptMode == 1
            }
            val kind = when (item.kind) {
                PayloadKind.Command -> if (item.scriptMode == 0) {
                    RootScriptPayloadKind.COMMAND
                } else {
                    RootScriptPayloadKind.SCRIPT
                }
                PayloadKind.RootManager -> RootScriptPayloadKind.ROOT_MANAGER
                PayloadKind.Lkm -> RootScriptPayloadKind.LKM
            }
            val args = when (item.kind) {
                PayloadKind.Command -> if (item.scriptMode == 0) {
                    PayloadScriptRenderer.tokenize(item.command)
                } else {
                    emptyList()
                }
                PayloadKind.Lkm -> PayloadScriptRenderer.tokenize(item.lkmParams)
                PayloadKind.RootManager -> {
                    val manager = requireNotNull(PayloadRootManagers.find(payload.rootManager)) {
                        "unknown root manager"
                    }
                    require(!manager.launchOnly && manager.binary.isNotBlank()) {
                        "launch-only root manager cannot produce a carrier preview"
                    }
                    manager.args + listOf("--kmi", "android0-0.0") +
                        listOfNotNull("--safe-mode".takeIf { payload.safeMode })
                }
            }
            RootScriptPayloadExecution(
                itemId = item.id,
                kind = kind,
                required = item.required,
                stagedPath = stagedPath,
                args = args,
                acceptance = when (item.acceptance) {
                    PayloadAcceptance.ExitZero -> RootScriptPayloadAcceptance.EXIT_ZERO
                    PayloadAcceptance.DaemonAck -> RootScriptPayloadAcceptance.DAEMON_ACK
                    PayloadAcceptance.ModulePresent -> RootScriptPayloadAcceptance.MODULE_PRESENT
                },
            )
        }
        val finishPlan = finish.items.map { item ->
            RootScriptFinishExecution(
                itemId = item.id,
                kind = when (item.kind) {
                    FinishKind.Command -> RootScriptFinishKind.COMMAND
                    FinishKind.Script -> RootScriptFinishKind.SCRIPT
                    FinishKind.Reboot -> RootScriptFinishKind.REBOOT
                },
                severity = when (item.severity) {
                    com.ghostlock.app.domain.finish.FinishSeverity.Normal -> RootScriptFinishSeverity.NORMAL
                    com.ghostlock.app.domain.finish.FinishSeverity.SafetyCritical ->
                        RootScriptFinishSeverity.SAFETY_CRITICAL
                },
                alwaysRun = item.alwaysRun,
                stagedPath = File(inputs, item.id).absolutePath.takeIf { item.kind == FinishKind.Script },
                args = item.args,
            )
        }
        return renderer.publish(
            workspace = workspace,
            plan = RootScriptExecutionPlan(
                runId = previewId,
                generation = 1L,
                payload = payloadPlan,
                finish = finishPlan,
            ),
        ).scriptPath
    }
}
