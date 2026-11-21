package com.ghostlock.app.data.runhost.host

import com.ghostlock.app.core.flow.PublishedFlowArtifact
import com.ghostlock.app.core.flow.RootScriptResultReader
import com.ghostlock.app.core.rootscript.RootScriptArtifact
import com.ghostlock.app.core.rootscript.RootScriptExecutionPlan
import com.ghostlock.app.core.runhost.ProductRootScriptPlanMapper
import com.ghostlock.app.core.runhost.RunHostIdentity
import com.ghostlock.app.core.runhost.RunHostRequestModel
import com.ghostlock.app.data.rootscript.AndroidRootScriptResultReader
import com.ghostlock.app.data.rootscript.RootScriptArtifactPublisher
import com.ghostlock.app.data.run.AndroidRunWorkspace
import com.ghostlock.app.data.run.RootArtifactStagingAdapter
import com.ghostlock.app.data.run.StagedRunInputArtifact
import com.ghostlock.app.domain.run.ProfileSnapshot
import com.ghostlock.app.domain.run.RunInputArtifactKind

/** Prepared product state shared by App and Shizuku RunHost implementations. */
data class PreparedProductRun(
    val plan: RootScriptExecutionPlan,
    val rootScript: RootScriptArtifact,
    val resultReader: RootScriptResultReader,
    val artifacts: Map<String, PublishedFlowArtifact>,
    val profile: ProfileSnapshot,
    val persistentStateDirectory: String,
)

/**
 * Android PRODUCT_ROOT staging and publication adapter.
 *
 * @param staging Sole host artifact catalog publisher.
 * @param planMapper Pure validated request-to-plan mapper.
 * @param publisher Sole physical root-script publisher.
 */
class ProductRunPreparer(
    private val staging: RootArtifactStagingAdapter,
    private val planMapper: ProductRootScriptPlanMapper = ProductRootScriptPlanMapper(),
    private val publisher: RootScriptArtifactPublisher = RootScriptArtifactPublisher(),
) {
    /**
     * Publishes one validated request and binds its single result reader.
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
        val plan = planMapper.map(identity, request, artifacts)
        val rootScript = publisher.publish(workspace.rootScript, plan)
        return PreparedProductRun(
            plan = plan,
            rootScript = rootScript,
            resultReader = AndroidRootScriptResultReader(rootScript, plan),
            artifacts = artifacts,
            profile = ProfileSnapshot(
                artifactId = request.profileArtifactId,
                schemaVersion = ProfileSnapshot.CURRENT_SCHEMA_VERSION,
                sha256 = profile.identity.sha256,
            ),
            persistentStateDirectory = workspace.persistentStateDirectory.absolutePath,
        )
    }
}
