package com.ghostlock.app.data.rootscript

import com.ghostlock.app.core.rootscript.PayloadScriptRenderer
import com.ghostlock.app.core.rootscript.RootScriptExecutionPlan
import com.ghostlock.app.core.rootscript.RootScriptFinishExecution
import com.ghostlock.app.core.rootscript.RootScriptFinishKind
import com.ghostlock.app.core.rootscript.RootScriptFinishSeverity
import com.ghostlock.app.core.rootscript.RootScriptPayloadAcceptance
import com.ghostlock.app.core.rootscript.RootScriptPayloadExecution
import com.ghostlock.app.core.rootscript.RootScriptPayloadKind
import com.ghostlock.app.domain.finish.FinishDefinition
import com.ghostlock.app.domain.finish.FinishKind
import com.ghostlock.app.domain.finish.FinishSeverity
import com.ghostlock.app.domain.payload.PayloadAcceptance
import com.ghostlock.app.domain.payload.PayloadDefinition
import com.ghostlock.app.domain.payload.PayloadKind
import com.ghostlock.app.domain.payload.PayloadRootManagers
import com.ghostlock.app.domain.run.RootScriptPreviewPort
import java.io.File
import java.util.UUID

/** Android preview adapter that maps typed documents to the production publisher. */
class PreviewRootScriptPublisher(
    private val previewRoot: File,
    private val publisher: RootScriptArtifactPublisher,
) : RootScriptPreviewPort {
    /**
     * Publishes an isolated preview without dispatching a RunHost.
     *
     * @param payload Exact Payload transaction result.
     * @param finish Explicit Finish definition; the Payload page supplies an empty value.
     * @return Absolute path of the published preview script.
     */
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
                    FinishSeverity.Normal -> RootScriptFinishSeverity.NORMAL
                    FinishSeverity.SafetyCritical -> RootScriptFinishSeverity.SAFETY_CRITICAL
                },
                alwaysRun = item.alwaysRun,
                stagedPath = File(inputs, item.id).absolutePath.takeIf { item.kind == FinishKind.Script },
                args = item.args,
            )
        }
        return publisher.publish(
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
