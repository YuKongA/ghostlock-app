package com.ghostlock.app.core.runhost

import com.ghostlock.app.core.flow.PublishedFlowArtifact
import com.ghostlock.app.core.rootscript.RootScriptExecutionPlan
import com.ghostlock.app.core.rootscript.RootScriptFinishExecution
import com.ghostlock.app.core.rootscript.RootScriptFinishKind
import com.ghostlock.app.core.rootscript.RootScriptFinishSeverity
import com.ghostlock.app.core.rootscript.RootScriptPayloadAcceptance
import com.ghostlock.app.core.rootscript.RootScriptPayloadExecution
import com.ghostlock.app.core.rootscript.RootScriptPayloadKind

/** Pure mapper from one validated RunHost request and staged catalog to the normalized product plan. */
class ProductRootScriptPlanMapper {
    /**
     * Maps and validates the sole root-script execution plan.
     *
     * @param identity Exact accepted run identity.
     * @param request Structurally and capability validated request.
     * @param artifacts Host-published staged catalog keyed by logical id.
     * @return Validated normalized Payload and Finish plan.
     */
    fun map(
        identity: RunHostIdentity,
        request: RunHostRequestModel,
        artifacts: Map<String, PublishedFlowArtifact>,
    ): RootScriptExecutionPlan = RootScriptExecutionPlan(
        runId = identity.runId,
        generation = identity.generation,
        payload = request.payloadItems.map { item -> item.toExecution(artifacts) },
        finish = request.finishItems.map { item -> item.toExecution(artifacts) },
        retainedArtifacts = emptyList(),
    ).also(RootScriptExecutionPlan::validate)

    /** Maps one validated Payload request to a fully resolved execution operation. */
    private fun RunHostPayloadItemRequest.toExecution(
        artifacts: Map<String, PublishedFlowArtifact>,
    ): RootScriptPayloadExecution {
        val artifact = artifactId?.let { logicalId ->
            requireNotNull(artifacts[logicalId]) { "Payload artifact was not staged" }
        }
        return RootScriptPayloadExecution(
            itemId = itemId,
            kind = when (kind) {
                RunHostPayloadKind.COMMAND -> RootScriptPayloadKind.COMMAND
                RunHostPayloadKind.SCRIPT -> RootScriptPayloadKind.SCRIPT
                RunHostPayloadKind.ROOT_MANAGER -> RootScriptPayloadKind.ROOT_MANAGER
                RunHostPayloadKind.LKM -> RootScriptPayloadKind.LKM
                RunHostPayloadKind.DETACHED_PROCESS -> RootScriptPayloadKind.DETACHED_PROCESS
            },
            required = required,
            stagedPath = artifact?.path,
            args = args.toList(),
            acceptance = when (acceptance) {
                RunHostPayloadAcceptance.EXIT_ZERO -> RootScriptPayloadAcceptance.EXIT_ZERO
                RunHostPayloadAcceptance.DAEMON_ACK -> RootScriptPayloadAcceptance.DAEMON_ACK
                RunHostPayloadAcceptance.MODULE_PRESENT -> RootScriptPayloadAcceptance.MODULE_PRESENT
                RunHostPayloadAcceptance.TAKEOVER_ACK -> RootScriptPayloadAcceptance.TAKEOVER_ACK
            },
        )
    }

    /** Maps one validated Finish request to a fully resolved execution operation. */
    private fun RunHostFinishItemRequest.toExecution(
        artifacts: Map<String, PublishedFlowArtifact>,
    ): RootScriptFinishExecution {
        val artifact = artifactId?.let { logicalId ->
            requireNotNull(artifacts[logicalId]) { "Finish artifact was not staged" }
        }
        return RootScriptFinishExecution(
            itemId = itemId,
            kind = when (kind) {
                RunHostFinishKind.COMMAND -> RootScriptFinishKind.COMMAND
                RunHostFinishKind.SCRIPT -> RootScriptFinishKind.SCRIPT
                RunHostFinishKind.REBOOT -> RootScriptFinishKind.REBOOT
            },
            severity = when (severity) {
                RunHostFinishSeverity.NORMAL -> RootScriptFinishSeverity.NORMAL
                RunHostFinishSeverity.SAFETY_CRITICAL -> RootScriptFinishSeverity.SAFETY_CRITICAL
            },
            alwaysRun = alwaysRun,
            stagedPath = artifact?.path,
            args = args.toList(),
        )
    }
}
