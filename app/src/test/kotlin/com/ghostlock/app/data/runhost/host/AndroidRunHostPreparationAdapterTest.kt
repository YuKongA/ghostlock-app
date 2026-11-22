package com.ghostlock.app.data.runhost.host

import android.os.ParcelFileDescriptor
import android.system.Os
import android.system.OsConstants
import android.system.StructStat
import com.ghostlock.app.core.flow.BackendFlowInput
import com.ghostlock.app.core.flow.Flow
import com.ghostlock.app.core.runhost.RunHostArtifactKind
import com.ghostlock.app.core.runhost.RunHostArtifactRequest
import com.ghostlock.app.core.runhost.RunHostCandidateResult
import com.ghostlock.app.core.runhost.RunHostCode
import com.ghostlock.app.core.runhost.RunHostIdentity
import com.ghostlock.app.core.runhost.RunHostRequestModel
import com.ghostlock.app.data.run.AndroidRunWorkspace
import com.ghostlock.app.data.run.OpenedRunInputArtifact
import com.ghostlock.app.data.runhost.plugin.LoadedPluginBackend
import com.ghostlock.app.data.runhost.plugin.PluginLoadResult
import com.ghostlock.app.data.runhost.plugin.PluginRunResourceOwner
import com.ghostlock.app.data.runhost.wire.OwnedRunHostRequestDescriptors
import com.ghostlock.app.domain.backend.AbortPolicy
import com.ghostlock.app.domain.backend.BackendDescriptor
import com.ghostlock.app.domain.backend.BackendSelectionPolicy
import com.ghostlock.app.domain.backend.ExecutionDomain
import com.ghostlock.app.domain.run.RunCapability
import com.ghostlock.app.domain.run.RunReport
import com.ghostlock.plugin.api.v1.DiagnosticBackendRun
import com.ghostlock.plugin.api.v1.DiagnosticRunContext
import com.ghostlock.plugin.api.v1.DiagnosticRunResult
import java.io.FileDescriptor
import java.io.FileInputStream
import java.io.IOException
import java.nio.file.Files
import java.security.MessageDigest
import java.util.concurrent.atomic.AtomicInteger
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import org.robolectric.annotation.Implementation
import org.robolectric.annotation.Implements
import org.robolectric.shadow.api.Shadow
import org.robolectric.util.ReflectionHelpers.ClassParameter

private typealias TestPluginLoader = (
    RunHostRequestModel,
    RunHostRole,
    AndroidRunWorkspace,
    ParcelFileDescriptor,
    (ExecutionDomain) -> Boolean,
) -> PluginLoadResult

/** Preparation owns descriptors/workspace while plugin resolution comes only from the loader result. */
@RunWith(RobolectricTestRunner::class)
@Config(shadows = [RegularFileFstatShadow::class])
class AndroidRunHostPreparationAdapterTest {
    private val identity = RunHostIdentity("1".repeat(32), 2L)

    @Test
    fun missingEnvironmentRejectsBeforeDescriptorDuplication() {
        val duplicateCalls = AtomicInteger(0)
        val adapter = adapter(environment = { null })

        val result = adapter.materialize(request(), identity) {
            duplicateCalls.incrementAndGet()
            error("must not duplicate")
        }

        assertEquals(RunHostCode.NOT_READY, rejected(result).code)
        assertEquals(0, duplicateCalls.get())
    }

    @Test
    fun invalidDescriptorRequestRejectsBeforeWorkspaceCreation() {
        val workspaceCalls = AtomicInteger(0)
        val adapter = adapter(
            environment = {
                AndroidRunHostEnvironment(10_000) { _, _ ->
                    workspaceCalls.incrementAndGet()
                    workspace()
                }
            },
        )

        val result = adapter.materialize(request(), identity) {
            throw IllegalArgumentException("invalid descriptor")
        }

        assertEquals(RunHostCode.INVALID_REQUEST, rejected(result).code)
        assertEquals(0, workspaceCalls.get())
    }

    @Test
    fun descriptorDuplicationIoFailureRejectsBeforeWorkspaceCreation() {
        val workspaceCalls = AtomicInteger(0)
        val adapter = adapter(
            environment = {
                AndroidRunHostEnvironment(10_000) { _, _ ->
                    workspaceCalls.incrementAndGet()
                    workspace()
                }
            },
        )

        val result = adapter.materialize(request(), identity) {
            throw IOException("dup failed")
        }

        assertEquals(RunHostCode.IO_FAILURE, rejected(result).code)
        assertEquals(0, workspaceCalls.get())
    }

    @Test
    fun unexpectedDescriptorFailureRejectsAsInternalContractBeforeWorkspaceCreation() {
        val workspaceCalls = AtomicInteger(0)
        val adapter = adapter(
            environment = {
                AndroidRunHostEnvironment(10_000) { _, _ ->
                    workspaceCalls.incrementAndGet()
                    workspace()
                }
            },
        )

        val result = adapter.materialize(request(), identity) {
            error("unexpected duplication failure")
        }

        assertEquals(RunHostCode.INTERNAL_CONTRACT_FAILURE, rejected(result).code)
        assertEquals(0, workspaceCalls.get())
    }

    @Test
    fun workspaceFailureClosesEveryOwnedDescriptor() {
        val source = Files.writeString(Files.createTempFile("host-adapter", ".profile"), "{}")
        val model = requestFor(source)
        val batch = descriptorBatch(model, source)
        val adapter = adapter(
            environment = {
                AndroidRunHostEnvironment(10_000) { _, _ -> throw IOException("mkdir failed") }
            },
        )

        val result = adapter.materialize(model, identity) { batch.holder }

        assertEquals(RunHostCode.IO_FAILURE, rejected(result).code)
        assertFalse(batch.plugin.fileDescriptor.valid())
        assertFalse(batch.artifact.fileDescriptor.valid())
    }

    @Test
    fun stagingIdentityFailureDeletesWorkspaceWithoutCallingLoader() {
        val source = Files.writeString(Files.createTempFile("host-adapter", ".profile"), "{}")
        val workspace = workspace()
        val valid = requestFor(source)
        val invalid = valid.copy(
            artifacts = listOf(valid.artifacts.single().copy(sha256 = "f".repeat(64))),
        )
        val batch = descriptorBatch(invalid, source)
        val loaderCalls = AtomicInteger(0)
        val adapter = adapter(
            environment = { environment(workspace) },
            loader = { _, _, _, _, _ ->
                loaderCalls.incrementAndGet()
                ready()
            },
        )

        val result = adapter.materialize(invalid, identity) { batch.holder }

        assertEquals(RunHostCode.ARTIFACT_INVALID, rejected(result).code)
        assertEquals(0, loaderCalls.get())
        assertFalse(workspace.root.exists())
        assertFalse(batch.plugin.fileDescriptor.valid())
        assertFalse(batch.artifact.fileDescriptor.valid())
    }

    @Test
    fun loaderRejectionPropagatesTaintAndDeletesWorkspace() {
        val source = Files.writeString(Files.createTempFile("host-adapter", ".profile"), "{}")
        val workspace = workspace()
        val model = requestFor(source)
        val batch = descriptorBatch(model, source)
        val adapter = adapter(
            environment = { environment(workspace) },
            loader = { _, _, _, _, _ ->
                PluginLoadResult.Rejected(RunHostCode.ARTIFACT_INVALID, pluginTainted = true)
            },
        )

        val rejection = rejected(adapter.materialize(model, identity) { batch.holder })

        assertEquals(RunHostCode.ARTIFACT_INVALID, rejection.code)
        assertTrue(rejection.pluginTainted)
        assertFalse(workspace.root.exists())
        assertFalse(batch.plugin.fileDescriptor.valid())
        assertFalse(batch.artifact.fileDescriptor.valid())
    }

    @Test
    fun loaderReceivesActualRoleAndDomainVerifierAfterStaging() {
        val source = Files.writeString(Files.createTempFile("host-adapter", ".profile"), "{}")
        val workspace = workspace()
        val model = requestFor(source)
        val batch = descriptorBatch(model, source)
        val observedRole = arrayOfNulls<RunHostRole>(1)
        val adapter = adapter(
            environment = { environment(workspace) },
            processMatches = { domain, role, applicationUid ->
                domain == ExecutionDomain.APP && role == RunHostRole.APP && applicationUid == 10_000
            },
            loader = { _, role, _, _, matches ->
                observedRole[0] = role
                if (matches(ExecutionDomain.APP)) ready()
                else PluginLoadResult.Rejected(RunHostCode.EXECUTION_DOMAIN_MISMATCH)
            },
        )

        val result = adapter.materialize(model, identity) { batch.holder }

        assertTrue(result is RunHostCandidateResult.Prepared)
        assertEquals(RunHostRole.APP, observedRole[0])
        val candidate = (result as RunHostCandidateResult.Prepared).candidate
        candidate.executionLease.close()
        assertTrue(candidate.workspace.deleteAfterTerminal())
    }

    @Test
    fun loadedBackendKeyMismatchClosesRunAndDeletesWorkspace() {
        val source = Files.writeString(Files.createTempFile("host-adapter", ".profile"), "{}")
        val workspace = workspace()
        val model = requestFor(source)
        val batch = descriptorBatch(model, source)
        val closeCalls = AtomicInteger(0)
        val resources = recordingResources(closeCalls)
        val adapter = adapter(
            environment = { environment(workspace) },
            loader = { _, _, _, _, _ -> ready(descriptor().copy(key = "other"), resources) },
        )

        val rejection = rejected(adapter.materialize(model, identity) { batch.holder })

        assertEquals(RunHostCode.INTERNAL_CONTRACT_FAILURE, rejection.code)
        assertTrue(rejection.pluginTainted)
        assertEquals(1, closeCalls.get())
        assertFalse(workspace.root.exists())
    }

    @Test
    fun diagnosticPublicationFailureClosesRunBeforeWorkspaceDeletion() {
        val source = Files.writeString(Files.createTempFile("host-adapter", ".profile"), "{}")
        val workspace = workspace()
        assertTrue(workspace.rootScript.delete())
        workspace.rootScript.writeText("not-a-directory")
        val model = requestFor(source)
        val batch = descriptorBatch(model, source)
        val closeCalls = AtomicInteger(0)
        val resources = recordingResources(closeCalls)
        val adapter = adapter(
            environment = { environment(workspace) },
            loader = { _, _, _, _, _ -> ready(resources = resources) },
        )

        val rejection = rejected(adapter.materialize(model, identity) { batch.holder })

        assertEquals(RunHostCode.IO_FAILURE, rejection.code)
        assertTrue(rejection.pluginTainted)
        assertEquals(1, closeCalls.get())
        assertFalse(workspace.root.exists())
    }

    @Test
    fun successfulDiagnosticTransfersExecutionAndWorkspaceOwnership() {
        val source = Files.writeString(Files.createTempFile("host-adapter", ".profile"), "{}")
        val workspace = workspace()
        val model = requestFor(source)
        val batch = descriptorBatch(model, source)
        val closeCalls = AtomicInteger(0)
        val resources = recordingResources(closeCalls)
        val adapter = adapter(
            environment = { environment(workspace) },
            loader = { _, _, _, _, _ -> ready(resources = resources) },
        )

        val result = adapter.materialize(model, identity) { batch.holder }

        assertTrue(result.toString(), result is RunHostCandidateResult.Prepared)
        assertFalse(batch.plugin.fileDescriptor.valid())
        assertFalse(batch.artifact.fileDescriptor.valid())
        assertTrue(workspace.root.exists())
        val candidate = (result as RunHostCandidateResult.Prepared).candidate
        assertEquals(identity, candidate.identity)
        assertEquals(RunCapability.DIAGNOSTIC_ONLY, candidate.reportContext.runCapability)
        candidate.executionLease.close()
        assertEquals(1, closeCalls.get())
        assertTrue(candidate.workspace.deleteAfterTerminal())
        assertFalse(workspace.root.exists())
    }

    private fun adapter(
        environment: () -> AndroidRunHostEnvironment? = { environment(workspace()) },
        processMatches: (ExecutionDomain, RunHostRole, Int) -> Boolean = { _, _, _ -> true },
        loader: TestPluginLoader = { _, _, _, _, _ -> ready() },
    ): AndroidRunHostPreparationAdapter = AndroidRunHostPreparationAdapter(
        role = RunHostRole.APP,
        hostEnvironment = environment,
        processMatches = processMatches,
        loadPlugin = { model, role, workspace, archive, matches ->
            try {
                loader(model, role, workspace, archive, matches)
            } finally {
                runCatching { archive.close() }
            }
        },
    )

    private fun ready(
        descriptor: BackendDescriptor = descriptor(),
        resources: PluginRunResourceOwner = PluginRunResourceOwner(),
    ): PluginLoadResult = PluginLoadResult.Ready(
        LoadedPluginBackend(descriptor, NoopFlow(), resources),
    )

    private fun descriptor(): BackendDescriptor = BackendDescriptor(
        key = "diagnose",
        selectionPolicy = BackendSelectionPolicy.SELECTABLE,
        abortPolicy = AbortPolicy.BEFORE_START_ONLY,
        executionDomain = ExecutionDomain.APP_OR_SHIZUKU,
        runCapability = RunCapability.DIAGNOSTIC_ONLY,
    )

    private fun recordingResources(closeCalls: AtomicInteger): PluginRunResourceOwner =
        PluginRunResourceOwner().also { resources ->
            resources.markClassLoading(requireNotNull(javaClass.classLoader))
            resources.register(object : DiagnosticBackendRun {
                override fun execute(context: DiagnosticRunContext): DiagnosticRunResult =
                    error("preparation must not execute the plugin")

                override fun close() {
                    closeCalls.incrementAndGet()
                }
            })
        }

    private fun environment(workspace: AndroidRunWorkspace): AndroidRunHostEnvironment =
        AndroidRunHostEnvironment(10_000) { _, _ -> workspace }

    private fun workspace(): AndroidRunWorkspace {
        val root = Files.createTempDirectory("host-adapter-workspace").toFile()
        val stat = Os.lstat(root.absolutePath)
        return AndroidRunWorkspace(
            root = root,
            stagedInputs = root.resolve("input").apply { mkdir() },
            executables = root.resolve("executable").apply { mkdir() },
            rootScript = root.resolve("root-script").apply { mkdir() },
            pluginPackages = root.resolve("plugin").apply { mkdir() },
            persistentStateDirectory = root.resolve("state").apply { mkdir() },
            rootDevice = stat.st_dev,
            rootInode = stat.st_ino,
            rootUid = stat.st_uid,
        )
    }

    private fun request(): RunHostRequestModel = RunHostRequestModel(
        runId = identity.runId,
        generation = identity.generation,
        backendKey = "diagnose",
        pluginContentSha256 = "a".repeat(64),
        profileArtifactId = "profile",
        payloadItems = emptyList(),
        finishItems = emptyList(),
        artifacts = listOf(
            RunHostArtifactRequest("profile", RunHostArtifactKind.PROFILE, 2L, "0".repeat(64)),
        ),
    )

    private fun requestFor(path: java.nio.file.Path): RunHostRequestModel = request().copy(
        artifacts = listOf(artifactRequestFor(path, "profile", RunHostArtifactKind.PROFILE)),
    )

    private fun artifactRequestFor(
        path: java.nio.file.Path,
        logicalId: String,
        kind: RunHostArtifactKind,
    ): RunHostArtifactRequest {
        val bytes = Files.readAllBytes(path)
        val digest = MessageDigest.getInstance("SHA-256")
            .digest(bytes)
            .joinToString("") { byte -> (byte.toInt() and 0xff).toString(16).padStart(2, '0') }
        return RunHostArtifactRequest(logicalId, kind, bytes.size.toLong(), digest)
    }

    private fun descriptorBatch(
        model: RunHostRequestModel,
        source: java.nio.file.Path,
    ): DescriptorBatch {
        val pluginPath = Files.writeString(Files.createTempFile("host-adapter", ".plugin"), "plugin")
        val plugin = openForStaging(pluginPath)
        val artifact = openForStaging(source)
        return DescriptorBatch(
            holder = OwnedRunHostRequestDescriptors(
                plugin,
                listOf(OpenedRunInputArtifact(model.artifacts.single(), artifact)),
            ),
            plugin = plugin,
            artifact = artifact,
        )
    }

    private fun openForStaging(path: java.nio.file.Path): ParcelFileDescriptor {
        val descriptor = Os.open(path.toString(), OsConstants.O_RDONLY or OsConstants.O_CLOEXEC, 0)
        return try {
            ParcelFileDescriptor.dup(descriptor)
        } finally {
            Os.close(descriptor)
        }
    }

    private fun rejected(result: RunHostCandidateResult): RunHostCandidateResult.Rejected =
        result as RunHostCandidateResult.Rejected

    private data class DescriptorBatch(
        val holder: OwnedRunHostRequestDescriptors,
        val plugin: ParcelFileDescriptor,
        val artifact: ParcelFileDescriptor,
    )

    private class NoopFlow : Flow {
        override suspend fun run(input: BackendFlowInput): RunReport = error("not started by preparation")

        override fun stop() = Unit
    }
}

/** Robolectric's host fstat omits Unix type bits; physical device tests retain the kernel check. */
@Implements(value = Os::class, callNativeMethodsByDefault = true)
internal class RegularFileFstatShadow {
    companion object {
        @JvmStatic
        @Implementation
        fun fstat(descriptor: FileDescriptor): StructStat {
            val stat = Shadow.directlyOn<StructStat, Os>(
                Os::class.java,
                "fstat",
                ClassParameter.from(FileDescriptor::class.java, descriptor),
            )
            var mode = stat.st_mode
            var size = stat.st_size
            if (mode and OsConstants.S_IFMT == 0) {
                runCatching { FileInputStream(descriptor).channel.size() }
                    .onSuccess { regularSize ->
                        mode = mode or OsConstants.S_IFREG
                        size = regularSize
                    }
            }
            return StructStat(
                stat.st_dev,
                stat.st_ino,
                mode,
                stat.st_nlink,
                stat.st_uid,
                stat.st_gid,
                stat.st_rdev,
                size,
                stat.st_atim,
                stat.st_mtim,
                stat.st_ctim,
                stat.st_blksize,
                stat.st_blocks,
            )
        }
    }
}
