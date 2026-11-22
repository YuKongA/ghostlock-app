package com.ghostlock.app.data.runhost.host

import android.system.Os
import android.os.ParcelFileDescriptor
import com.ghostlock.app.core.flow.Flow
import com.ghostlock.app.core.flow.FlowReportContext
import com.ghostlock.app.core.runhost.RunHostCandidateResult
import com.ghostlock.app.core.runhost.RunHostCode
import com.ghostlock.app.core.runhost.RunHostIdentity
import com.ghostlock.app.core.runhost.RunHostPreparedCandidate
import com.ghostlock.app.core.runhost.RunHostRequestModel
import com.ghostlock.app.core.runhost.RunHostRequestValidation
import com.ghostlock.app.core.runhost.RunHostRequestValidator
import com.ghostlock.app.core.runhost.RunHostWorkspaceLease
import com.ghostlock.app.data.rootscript.RootScriptArtifactPublisher
import com.ghostlock.app.data.run.AndroidRunWorkspace
import com.ghostlock.app.data.run.OpenedRunInputArtifact
import com.ghostlock.app.data.run.RootArtifactStagingAdapter
import com.ghostlock.app.domain.backend.ExecutionDomain
import com.ghostlock.app.domain.run.ProfileSnapshot
import com.ghostlock.app.domain.run.RunCapability
import com.ghostlock.app.data.runhost.plugin.PluginLoadResult
import com.ghostlock.app.data.runhost.plugin.RunHostPluginLoader
import com.ghostlock.app.data.runhost.wire.OwnedRunHostRequestDescriptors
import java.io.IOException

/** Live platform facts and the role-specific physical workspace constructor. */
internal data class AndroidRunHostEnvironment(
    val applicationUid: Int,
    val createWorkspace: (RunHostIdentity, String) -> AndroidRunWorkspace,
)

/** Android resource adapter that materializes one total prepared-or-rejected candidate. */
internal class AndroidRunHostPreparationAdapter(
    private val role: RunHostRole,
    private val hostEnvironment: () -> AndroidRunHostEnvironment?,
    private val processMatches: (ExecutionDomain, RunHostRole, Int) -> Boolean =
        { expected, actualRole, applicationUid ->
            ExecutionDomainVerifier.currentProcessMatches(expected, actualRole, applicationUid)
        },
    private val staging: RootArtifactStagingAdapter = RootArtifactStagingAdapter(),
    private val rootScriptPublisher: RootScriptArtifactPublisher = RootScriptArtifactPublisher(),
    private val productPreparer: ProductRunPreparer =
        ProductRunPreparer(staging, publisher = rootScriptPublisher),
    private val loadPlugin: (
        RunHostRequestModel,
        RunHostRole,
        AndroidRunWorkspace,
        ParcelFileDescriptor,
        (ExecutionDomain) -> Boolean,
    ) -> PluginLoadResult = { model, hostRole, workspace, archive, domainMatches ->
        RunHostPluginLoader().load(model, hostRole, workspace, archive, domainMatches)
    },
) {
    /** Resolves runtime/platform facts, transfers PFDs, and returns a fully owned candidate without throwing. */
    fun materialize(
        model: RunHostRequestModel,
        identity: RunHostIdentity,
        duplicateDescriptors: () -> OwnedRunHostRequestDescriptors,
    ): RunHostCandidateResult {
        val environment = runCatching(hostEnvironment)
            .getOrElse {
                return RunHostCandidateResult.Rejected(RunHostCode.INTERNAL_CONTRACT_FAILURE)
            }
            ?: return RunHostCandidateResult.Rejected(RunHostCode.NOT_READY)

        val ownedDescriptors = try {
            duplicateDescriptors()
        } catch (_: IllegalArgumentException) {
            return RunHostCandidateResult.Rejected(RunHostCode.INVALID_REQUEST)
        } catch (_: IOException) {
            return RunHostCandidateResult.Rejected(RunHostCode.IO_FAILURE)
        } catch (_: Throwable) {
            return RunHostCandidateResult.Rejected(RunHostCode.INTERNAL_CONTRACT_FAILURE)
        }
        val workspace = try {
            environment.createWorkspace(identity, model.backendKey)
        } catch (problem: IllegalArgumentException) {
            ownedDescriptors.close()
            return RunHostCandidateResult.Rejected(RunHostCode.INTERNAL_CONTRACT_FAILURE)
        } catch (problem: Throwable) {
            ownedDescriptors.close()
            return RunHostCandidateResult.Rejected(RunHostCode.IO_FAILURE)
        }

        val artifacts = try {
            ownedDescriptors.takeArtifacts()
        } catch (_: Throwable) {
            ownedDescriptors.close()
            workspace.deleteAfterTerminal()
            return RunHostCandidateResult.Rejected(RunHostCode.INTERNAL_CONTRACT_FAILURE)
        }
        val staged = try {
            staging.stage(workspace, artifacts)
        } catch (problem: IllegalArgumentException) {
            ownedDescriptors.close()
            workspace.deleteAfterTerminal()
            return RunHostCandidateResult.Rejected(RunHostCode.ARTIFACT_INVALID)
        } catch (problem: Throwable) {
            ownedDescriptors.close()
            workspace.deleteAfterTerminal()
            return RunHostCandidateResult.Rejected(RunHostCode.IO_FAILURE)
        }

        val archive = try {
            ownedDescriptors.takePluginArchive()
        } catch (_: Throwable) {
            ownedDescriptors.close()
            workspace.deleteAfterTerminal()
            return RunHostCandidateResult.Rejected(RunHostCode.INTERNAL_CONTRACT_FAILURE)
        }
        val loaded = try {
            loadPlugin(
                model,
                role,
                workspace,
                archive,
                { domain ->
                    processMatches(domain, role, environment.applicationUid)
                },
            )
        } catch (_: Throwable) {
            workspace.deleteAfterTerminal()
            return RunHostCandidateResult.Rejected(RunHostCode.INTERNAL_CONTRACT_FAILURE)
        } finally {
            ownedDescriptors.close()
        }
        if (loaded is PluginLoadResult.Rejected) {
            workspace.deleteAfterTerminal()
            return RunHostCandidateResult.Rejected(loaded.code, loaded.pluginTainted)
        }
        val backend = (loaded as PluginLoadResult.Ready).backend
        if (backend.descriptor.key != model.backendKey) {
            backend.resources.close()
            workspace.deleteAfterTerminal()
            return RunHostCandidateResult.Rejected(
                RunHostCode.INTERNAL_CONTRACT_FAILURE,
                pluginTainted = true,
            )
        }

        val materialized = when (backend.descriptor.runCapability) {
            RunCapability.PRODUCT_ROOT -> prepareProduct(workspace, identity, model, staged)
            RunCapability.DIAGNOSTIC_ONLY -> prepareDiagnostic(workspace, identity, model, staged)
            RunCapability.UNRESOLVED -> PreparedParts.Rejected(RunHostCode.INTERNAL_CONTRACT_FAILURE)
        }
        if (materialized is PreparedParts.Rejected) {
            backend.resources.close()
            workspace.deleteAfterTerminal()
            return RunHostCandidateResult.Rejected(materialized.code, pluginTainted = true)
        }

        val parts = materialized as PreparedParts.Ready
        return runCatching {
            RunHostCandidateResult.Prepared(
                RunHostPreparedCandidate(
                    identity = identity,
                    abortPolicy = backend.descriptor.abortPolicy,
                    flow = backend.flow,
                    executionLease = backend.resources,
                    workspace = AndroidWorkspaceLease(workspace),
                    rootScript = parts.rootScript,
                    profile = parts.profile,
                    artifacts = parts.artifacts,
                    persistentStateDirectory = parts.persistentStateDirectory,
                    reportContext = parts.reportContext,
                ),
            )
        }.getOrElse {
            backend.resources.close()
            workspace.deleteAfterTerminal()
            RunHostCandidateResult.Rejected(
                RunHostCode.INTERNAL_CONTRACT_FAILURE,
                pluginTainted = true,
            )
        }
    }

    /** Builds product catalog/plan facts separately from publication I/O failure semantics. */
    private fun prepareProduct(
        workspace: AndroidRunWorkspace,
        identity: RunHostIdentity,
        model: RunHostRequestModel,
        staged: List<com.ghostlock.app.data.run.StagedRunInputArtifact>,
    ): PreparedParts = try {
        val product = productPreparer.prepare(workspace, identity, model, staged)
        PreparedParts.Ready(
            rootScript = product.rootScript,
            profile = product.profile,
            artifacts = product.artifacts,
            persistentStateDirectory = product.persistentStateDirectory,
            reportContext = FlowReportContext(
                runCapability = RunCapability.PRODUCT_ROOT,
                productPlan = product.plan,
                productResultReader = product.resultReader,
            ),
        )
    } catch (problem: IllegalArgumentException) {
        PreparedParts.Rejected(RunHostCode.INTERNAL_CONTRACT_FAILURE)
    } catch (problem: Throwable) {
        PreparedParts.Rejected(RunHostCode.IO_FAILURE)
    }

    /** Builds the diagnostic catalog before performing physical script publication. */
    private fun prepareDiagnostic(
        workspace: AndroidRunWorkspace,
        identity: RunHostIdentity,
        model: RunHostRequestModel,
        staged: List<com.ghostlock.app.data.run.StagedRunInputArtifact>,
    ): PreparedParts {
        val artifacts = runCatching { staging.publishCatalog(staged) }
            .getOrElse { return PreparedParts.Rejected(RunHostCode.INTERNAL_CONTRACT_FAILURE) }
        val profileArtifact = artifacts[model.profileArtifactId]
            ?: return PreparedParts.Rejected(RunHostCode.INTERNAL_CONTRACT_FAILURE)
        val rootScript = runCatching { rootScriptPublisher.publishDiagnostic(workspace.rootScript, identity) }
            .getOrElse { return PreparedParts.Rejected(RunHostCode.IO_FAILURE) }
        val profile = runCatching {
            ProfileSnapshot(
                artifactId = model.profileArtifactId,
                schemaVersion = ProfileSnapshot.CURRENT_SCHEMA_VERSION,
                sha256 = profileArtifact.identity.sha256,
            )
        }.getOrElse { return PreparedParts.Rejected(RunHostCode.INTERNAL_CONTRACT_FAILURE) }
        return PreparedParts.Ready(
            rootScript = rootScript,
            profile = profile,
            artifacts = artifacts,
            persistentStateDirectory = workspace.persistentStateDirectory.absolutePath,
            reportContext = FlowReportContext(
                runCapability = RunCapability.DIAGNOSTIC_ONLY,
                productPlan = null,
                productResultReader = null,
            ),
        )
    }

    /** Private Android wrapper that keeps File and Os dependencies outside the controller. */
    private class AndroidWorkspaceLease(
        private val workspace: AndroidRunWorkspace,
    ) : RunHostWorkspaceLease {
        override fun activateExecutionEnvironment() {
            Os.setenv("GHOSTLOCK_HOME", workspace.root.absolutePath, true)
        }

        override fun deleteAfterTerminal(): Boolean = workspace.deleteAfterTerminal()
    }

    /** Stage-local result used to retain the exact failure mapping before candidate construction. */
    private sealed interface PreparedParts {
        data class Ready(
            val rootScript: com.ghostlock.app.core.rootscript.RootScriptArtifact,
            val profile: ProfileSnapshot,
            val artifacts: Map<String, com.ghostlock.app.core.flow.PublishedFlowArtifact>,
            val persistentStateDirectory: String,
            val reportContext: FlowReportContext,
        ) : PreparedParts

        data class Rejected(val code: RunHostCode) : PreparedParts
    }
}
