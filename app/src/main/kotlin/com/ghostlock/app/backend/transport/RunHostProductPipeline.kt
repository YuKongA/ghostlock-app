package com.ghostlock.app.backend.transport

import com.ghostlock.app.core.flow.PublishedFlowArtifact
import com.ghostlock.app.core.rootscript.RootScriptArtifact
import com.ghostlock.app.core.rootscript.RootScriptExecutionPlan
import com.ghostlock.app.core.rootscript.RootScriptFinishExecution
import com.ghostlock.app.core.rootscript.RootScriptFinishKind
import com.ghostlock.app.core.rootscript.RootScriptFinishSeverity
import com.ghostlock.app.core.rootscript.RootScriptPayloadAcceptance
import com.ghostlock.app.core.rootscript.RootScriptPayloadExecution
import com.ghostlock.app.core.rootscript.RootScriptPayloadKind
import com.ghostlock.app.core.rootscript.RootScriptRenderer
import com.ghostlock.app.data.run.AndroidRunWorkspace
import com.ghostlock.app.data.run.RootArtifactStagingAdapter
import com.ghostlock.app.data.run.StagedRunInputArtifact
import com.ghostlock.app.domain.run.ProfileSnapshot
import com.ghostlock.app.domain.run.RunInputArtifactKind
import com.ghostlock.app.domain.run.RunTerminal

/** Prepared product state shared by App and Shizuku RunHost implementations. */
data class PreparedProductRun(
    val plan: RootScriptExecutionPlan,
    val rootScript: RootScriptArtifact,
    val artifacts: Map<String, PublishedFlowArtifact>,
    val profile: ProfileSnapshot,
    val persistentStateDirectory: String,
)

/**
 * Shared PRODUCT_ROOT publication path.
 *
 * @param staging Sole host artifact catalog publisher.
 * @param renderer Sole normalized root-script renderer.
 */
class RunHostProductPipeline(
    private val staging: RootArtifactStagingAdapter,
    private val renderer: RootScriptRenderer = RootScriptRenderer(),
) {
    /**
     * Maps one already validated request and staged-object list to the sole execution plan.
     *
     * @param workspace Fresh host-owned workspace.
     * @param identity Exact run identity.
     * @param request Structurally and capability validated request model.
     * @param staged Staged objects produced from owned request descriptors.
     * @return Fully published product state consumed by one fresh Flow.
     */
    fun prepare(
        workspace: AndroidRunWorkspace,
        identity: RunHostIdentity,
        request: RunHostRequestModel,
        staged: List<StagedRunInputArtifact>,
    ): PreparedProductRun {
        val artifacts = staging.publishCatalog(staged)
        val profile = requireNotNull(artifacts[request.profileArtifactId]) {
            "staged profile artifact is missing"
        }
        require(profile.kind == RunInputArtifactKind.PROFILE) { "profile artifact kind changed" }
        val plan = RootScriptExecutionPlan(
            runId = identity.runId,
            generation = identity.generation,
            payload = request.payloadItems.map { item -> item.toExecution(artifacts) },
            finish = request.finishItems.map { item -> item.toExecution(artifacts) },
            retainedArtifacts = emptyList(),
        )
        plan.validate()
        val rootScript = renderer.publish(workspace.rootScript, plan)
        return PreparedProductRun(
            plan = plan,
            rootScript = rootScript,
            artifacts = artifacts,
            profile = ProfileSnapshot(
                artifactId = request.profileArtifactId,
                schemaVersion = ProfileSnapshot.CURRENT_SCHEMA_VERSION,
                sha256 = profile.identity.sha256,
            ),
            persistentStateDirectory = workspace.persistentStateDirectory.absolutePath,
        )
    }

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

/** Returns whether a terminal requires retaining the host workspace as recovery/evidence state. */
internal fun RunTerminal.requiresWorkspaceRetention(): Boolean = this in setOf(
    RunTerminal.UNKNOWN,
    RunTerminal.NEEDS_ATTENTION,
    RunTerminal.REBOOT_REQUIRED,
    RunTerminal.REBOOT_REQUESTED,
)
