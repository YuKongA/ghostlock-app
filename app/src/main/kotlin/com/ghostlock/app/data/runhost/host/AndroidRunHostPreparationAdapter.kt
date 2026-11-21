package com.ghostlock.app.data.runhost.host

import android.system.Os
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
import com.ghostlock.app.domain.backend.BackendAvailability
import com.ghostlock.app.domain.backend.BackendDescriptor
import com.ghostlock.app.domain.backend.ExecutionDomain
import com.ghostlock.app.domain.run.ProfileSnapshot
import com.ghostlock.app.domain.run.RunCapability

/** Resource-free runtime projection supplied directly by one framework entry. */
internal data class AndroidRunHostRuntime(
    val descriptor: BackendDescriptor,
    val availability: () -> BackendAvailability,
    val createFlow: () -> Flow,
)

/** Live platform facts and the role-specific physical workspace constructor. */
internal data class AndroidRunHostEnvironment(
    val applicationUid: Int,
    val createWorkspace: (RunHostIdentity, String) -> AndroidRunWorkspace,
)

/** Android resource adapter that materializes one total prepared-or-rejected candidate. */
internal class AndroidRunHostPreparationAdapter(
    private val role: RunHostRole,
    private val hostEnvironment: () -> AndroidRunHostEnvironment?,
    private val resolveRuntime: (String) -> AndroidRunHostRuntime?,
    private val processMatches: (ExecutionDomain, RunHostRole, Int) -> Boolean =
        { expected, actualRole, applicationUid ->
            ExecutionDomainVerifier.currentProcessMatches(expected, actualRole, applicationUid)
        },
    private val staging: RootArtifactStagingAdapter = RootArtifactStagingAdapter(),
    private val rootScriptPublisher: RootScriptArtifactPublisher = RootScriptArtifactPublisher(),
    private val productPreparer: ProductRunPreparer =
        ProductRunPreparer(staging, publisher = rootScriptPublisher),
) {
    /** Resolves runtime/platform facts, transfers PFDs, and returns a fully owned candidate without throwing. */
    fun materialize(
        model: RunHostRequestModel,
        identity: RunHostIdentity,
        duplicateDescriptors: () -> List<OpenedRunInputArtifact>,
    ): RunHostCandidateResult {
        val runtime = runCatching { resolveRuntime(model.backendKey) }
            .getOrElse {
                return RunHostCandidateResult.Rejected(RunHostCode.INTERNAL_CONTRACT_FAILURE)
            }
            ?: return RunHostCandidateResult.Rejected(RunHostCode.UNKNOWN_BACKEND)
        if (runtime.descriptor.key != model.backendKey) {
            return RunHostCandidateResult.Rejected(RunHostCode.INTERNAL_CONTRACT_FAILURE)
        }
        val shape = runCatching {
            RunHostRequestValidator.validateCapability(model, runtime.descriptor.runCapability)
        }.getOrElse {
            return RunHostCandidateResult.Rejected(RunHostCode.INTERNAL_CONTRACT_FAILURE)
        }
        when (shape) {
            RunHostRequestValidation.Valid -> Unit
            is RunHostRequestValidation.Invalid -> return RunHostCandidateResult.Rejected(shape.code)
        }
        val environment = runCatching(hostEnvironment)
            .getOrElse {
                return RunHostCandidateResult.Rejected(RunHostCode.INTERNAL_CONTRACT_FAILURE)
            }
            ?: return RunHostCandidateResult.Rejected(RunHostCode.NOT_READY)
        val domainMatches = runCatching {
            processMatches(runtime.descriptor.executionDomain, role, environment.applicationUid)
        }.getOrDefault(false)
        if (!domainMatches) {
            return RunHostCandidateResult.Rejected(RunHostCode.EXECUTION_DOMAIN_MISMATCH)
        }
        val availability = runCatching(runtime.availability)
            .getOrElse {
                return RunHostCandidateResult.Rejected(RunHostCode.INTERNAL_CONTRACT_FAILURE)
            }
        if (availability is BackendAvailability.NotReady) {
            return RunHostCandidateResult.Rejected(RunHostCode.NOT_READY)
        }

        val ownedDescriptors = runCatching(duplicateDescriptors)
            .getOrElse { return RunHostCandidateResult.Rejected(RunHostCode.ARTIFACT_INVALID) }
        val workspace = try {
            environment.createWorkspace(identity, runtime.descriptor.key)
        } catch (problem: IllegalArgumentException) {
            closeOwnedDescriptors(ownedDescriptors)
            return RunHostCandidateResult.Rejected(RunHostCode.INTERNAL_CONTRACT_FAILURE)
        } catch (problem: Throwable) {
            closeOwnedDescriptors(ownedDescriptors)
            return RunHostCandidateResult.Rejected(RunHostCode.IO_FAILURE)
        }

        val staged = try {
            staging.stage(workspace, ownedDescriptors)
        } catch (problem: IllegalArgumentException) {
            workspace.deleteAfterTerminal()
            return RunHostCandidateResult.Rejected(RunHostCode.ARTIFACT_INVALID)
        } catch (problem: Throwable) {
            workspace.deleteAfterTerminal()
            return RunHostCandidateResult.Rejected(RunHostCode.IO_FAILURE)
        }

        val materialized = when (runtime.descriptor.runCapability) {
            RunCapability.PRODUCT_ROOT -> prepareProduct(workspace, identity, model, staged)
            RunCapability.DIAGNOSTIC_ONLY -> prepareDiagnostic(workspace, identity, model, staged)
            RunCapability.UNRESOLVED -> PreparedParts.Rejected(RunHostCode.INTERNAL_CONTRACT_FAILURE)
        }
        if (materialized is PreparedParts.Rejected) {
            workspace.deleteAfterTerminal()
            return RunHostCandidateResult.Rejected(materialized.code)
        }

        val parts = materialized as PreparedParts.Ready
        val flow = runCatching(runtime.createFlow)
            .getOrElse {
                workspace.deleteAfterTerminal()
                return RunHostCandidateResult.Rejected(RunHostCode.IO_FAILURE)
            }
        return runCatching {
            RunHostCandidateResult.Prepared(
                RunHostPreparedCandidate(
                    identity = identity,
                    abortPolicy = runtime.descriptor.abortPolicy,
                    flow = flow,
                    workspace = AndroidWorkspaceLease(workspace),
                    rootScript = parts.rootScript,
                    profile = parts.profile,
                    artifacts = parts.artifacts,
                    persistentStateDirectory = parts.persistentStateDirectory,
                    reportContext = parts.reportContext,
                ),
            )
        }.getOrElse {
            workspace.deleteAfterTerminal()
            RunHostCandidateResult.Rejected(RunHostCode.INTERNAL_CONTRACT_FAILURE)
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

    /** Closes only descriptors still owned by the adapter before staging takes the whole batch. */
    private fun closeOwnedDescriptors(inputs: List<OpenedRunInputArtifact>) {
        inputs.forEach { input -> runCatching { input.descriptor.close() } }
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
