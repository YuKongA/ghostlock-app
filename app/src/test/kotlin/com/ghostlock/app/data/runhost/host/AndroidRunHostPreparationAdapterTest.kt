package com.ghostlock.app.data.runhost.host

import android.os.ParcelFileDescriptor
import android.system.Os
import android.system.OsConstants
import android.system.StructStat
import com.ghostlock.app.core.flow.BackendFlowInput
import com.ghostlock.app.core.flow.Flow
import com.ghostlock.app.core.rootscript.ROOT_SCRIPT_HANDOFF_FILE_NAME
import com.ghostlock.app.core.runhost.RunHostArtifactKind
import com.ghostlock.app.core.runhost.RunHostArtifactRequest
import com.ghostlock.app.core.runhost.RunHostCandidateResult
import com.ghostlock.app.core.runhost.RunHostCode
import com.ghostlock.app.core.runhost.RunHostIdentity
import com.ghostlock.app.core.runhost.RunHostPayloadAcceptance
import com.ghostlock.app.core.runhost.RunHostPayloadItemRequest
import com.ghostlock.app.core.runhost.RunHostPayloadKind
import com.ghostlock.app.core.runhost.RunHostRequestModel
import com.ghostlock.app.data.run.AndroidRunWorkspace
import com.ghostlock.app.data.run.OpenedRunInputArtifact
import com.ghostlock.app.domain.backend.AbortPolicy
import com.ghostlock.app.domain.backend.BackendAvailability
import com.ghostlock.app.domain.backend.BackendAvailabilityReason
import com.ghostlock.app.domain.backend.BackendDescriptor
import com.ghostlock.app.domain.backend.BackendSelectionPolicy
import com.ghostlock.app.domain.backend.ExecutionDomain
import com.ghostlock.app.domain.run.RunCapability
import com.ghostlock.app.domain.run.RunReport
import java.io.IOException
import java.io.FileDescriptor
import java.io.FileInputStream
import java.security.MessageDigest
import java.nio.file.Files
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

/** Android preparation stage mapping and descriptor/workspace ownership cases. */
@RunWith(RobolectricTestRunner::class)
@Config(shadows = [RegularFileFstatShadow::class])
class AndroidRunHostPreparationAdapterTest {
    private val identity = RunHostIdentity("1".repeat(32), 2L)

    @Test
    fun missingLiveEnvironmentPreservesRuntimeAndCapabilityPrecedence() {
        val duplicateCalls = AtomicInteger(0)
        val missingBackend = adapter(environment = { null }, runtime = { null })
        assertEquals(
            RunHostCode.UNKNOWN_BACKEND,
            rejectedCode(missingBackend.materialize(request(), identity) {
                duplicateCalls.incrementAndGet()
                emptyList()
            }),
        )

        val invalidCapability = adapter(environment = { null }, runtime = {
            val resolved = runtime()
            resolved.copy(
                descriptor = resolved.descriptor.copy(runCapability = RunCapability.PRODUCT_ROOT),
            )
        })
        assertEquals(
            RunHostCode.INVALID_REQUEST,
            rejectedCode(invalidCapability.materialize(request(), identity) {
                duplicateCalls.incrementAndGet()
                emptyList()
            }),
        )

        val validRuntime = adapter(environment = { null })
        assertEquals(
            RunHostCode.NOT_READY,
            rejectedCode(validRuntime.materialize(request(), identity) {
                duplicateCalls.incrementAndGet()
                emptyList()
            }),
        )
        assertEquals(0, duplicateCalls.get())
    }

    @Test
    fun unknownBackendRejectsBeforeDescriptorDuplication() {
        val duplicateCalls = AtomicInteger(0)
        val adapter = adapter(runtime = { null })
        val result = adapter.materialize(request(), identity) {
            duplicateCalls.incrementAndGet()
            emptyList()
        }
        assertEquals(RunHostCode.UNKNOWN_BACKEND, rejectedCode(result))
        assertEquals(0, duplicateCalls.get())
    }

    @Test
    fun runtimeProjectionKeyMismatchMapsInternalBeforeDescriptorDuplication() {
        val duplicateCalls = AtomicInteger(0)
        val adapter = adapter(runtime = {
            val resolved = runtime()
            resolved.copy(descriptor = resolved.descriptor.copy(key = "other"))
        })
        val result = adapter.materialize(request(), identity) {
            duplicateCalls.incrementAndGet()
            emptyList()
        }
        assertEquals(RunHostCode.INTERNAL_CONTRACT_FAILURE, rejectedCode(result))
        assertEquals(0, duplicateCalls.get())
    }

    @Test
    fun capabilityShapeFailureReturnsValidatorCodeBeforeDescriptorDuplication() {
        val duplicateCalls = AtomicInteger(0)
        val adapter = adapter(runtime = {
            val resolved = runtime()
            resolved.copy(
                descriptor = resolved.descriptor.copy(runCapability = RunCapability.PRODUCT_ROOT),
            )
        })
        val result = adapter.materialize(request(), identity) {
            duplicateCalls.incrementAndGet()
            emptyList()
        }

        assertEquals(RunHostCode.INVALID_REQUEST, rejectedCode(result))
        assertEquals(0, duplicateCalls.get())
    }

    @Test
    fun executionDomainMismatchRejectsBeforeDescriptorDuplication() {
        val duplicateCalls = AtomicInteger(0)
        val adapter = adapter(processMatches = { _, _, _ -> false })
        val result = adapter.materialize(request(), identity) {
            duplicateCalls.incrementAndGet()
            emptyList()
        }
        assertEquals(RunHostCode.EXECUTION_DOMAIN_MISMATCH, rejectedCode(result))
        assertEquals(0, duplicateCalls.get())
    }

    @Test
    fun notReadyAndThrowingAvailabilityAreTotalBeforeDescriptorDuplication() {
        val duplicateCalls = AtomicInteger(0)
        val notReady = adapter(runtime = {
            runtime(
                availability = {
                    BackendAvailability.NotReady(
                        listOf(BackendAvailabilityReason("missing", "missing")),
                    )
                },
            )
        })
        assertEquals(
            RunHostCode.NOT_READY,
            rejectedCode(notReady.materialize(request(), identity) {
                duplicateCalls.incrementAndGet()
                emptyList()
            }),
        )

        val throwing = adapter(runtime = {
            runtime(availability = { error("readiness contract failed") })
        })
        assertEquals(
            RunHostCode.INTERNAL_CONTRACT_FAILURE,
            rejectedCode(throwing.materialize(request(), identity) {
                duplicateCalls.incrementAndGet()
                emptyList()
            }),
        )
        assertEquals(0, duplicateCalls.get())
    }

    @Test
    fun descriptorDuplicationFailureMapsArtifactInvalidBeforeWorkspaceCreation() {
        val workspaceCalls = AtomicInteger(0)
        val adapter = adapter(
            environment = {
                AndroidRunHostEnvironment(10_000) { _, _ ->
                    workspaceCalls.incrementAndGet()
                    workspace()
                }
            },
        )
        val result = adapter.materialize(request(), identity) { error("dup failed") }
        assertEquals(RunHostCode.ARTIFACT_INVALID, rejectedCode(result))
        assertEquals(0, workspaceCalls.get())
    }

    @Test
    fun physicalWorkspaceFailureClosesOwnedDescriptorsAndMapsIoFailure() {
        val source = Files.writeString(Files.createTempFile("host-adapter", ".profile"), "{}")
        val owned = ParcelFileDescriptor.open(source.toFile(), ParcelFileDescriptor.MODE_READ_ONLY)
        val adapter = adapter(
            environment = {
                AndroidRunHostEnvironment(10_000) { _, _ -> throw IOException("mkdir failed") }
            },
        )

        val result = adapter.materialize(requestFor(source), identity) {
            listOf(OpenedRunInputArtifact(requestFor(source).artifacts.single(), owned))
        }

        assertEquals(RunHostCode.IO_FAILURE, rejectedCode(result))
        assertFalse(owned.fileDescriptor.valid())
    }

    @Test
    fun verifiedNamespacePreconditionFailureClosesDescriptorsAndMapsInternalFailure() {
        val source = Files.writeString(Files.createTempFile("host-adapter", ".profile"), "{}")
        val owned = ParcelFileDescriptor.open(source.toFile(), ParcelFileDescriptor.MODE_READ_ONLY)
        val adapter = adapter(
            environment = {
                AndroidRunHostEnvironment(10_000) { _, _ ->
                    throw IllegalArgumentException("namespace changed")
                }
            },
        )

        val result = adapter.materialize(requestFor(source), identity) {
            listOf(OpenedRunInputArtifact(requestFor(source).artifacts.single(), owned))
        }

        assertEquals(RunHostCode.INTERNAL_CONTRACT_FAILURE, rejectedCode(result))
        assertFalse(owned.fileDescriptor.valid())
    }

    @Test
    fun stagingIdentityFailureMapsArtifactInvalidAndDeletesWorkspace() {
        val source = Files.writeString(Files.createTempFile("host-adapter", ".profile"), "{}")
        val workspace = workspace()
        val invalid = requestFor(source).copy(
            artifacts = listOf(requestFor(source).artifacts.single().copy(sha256 = "f".repeat(64))),
        )
        val adapter = adapter(environment = { environment(workspace) })
        val result = adapter.materialize(invalid, identity) {
            listOf(
                OpenedRunInputArtifact(
                    invalid.artifacts.single(),
                    ParcelFileDescriptor.open(source.toFile(), ParcelFileDescriptor.MODE_READ_ONLY),
                ),
            )
        }

        assertEquals(RunHostCode.ARTIFACT_INVALID, rejectedCode(result))
        assertFalse(workspace.root.exists())
    }

    @Test
    fun stagingPhysicalFailureMapsIoClosesDescriptorAndDeletesWorkspace() {
        val source = Files.writeString(Files.createTempFile("host-adapter", ".profile"), "{}")
        val workspace = workspace()
        assertTrue(workspace.stagedInputs.delete())
        workspace.stagedInputs.writeText("not-a-directory")
        val owned = openForStaging(source)
        val adapter = adapter(environment = { environment(workspace) })
        val model = requestFor(source)
        val result = adapter.materialize(model, identity) {
            listOf(OpenedRunInputArtifact(model.artifacts.single(), owned))
        }
        assertEquals(RunHostCode.IO_FAILURE, rejectedCode(result))
        assertFalse(owned.fileDescriptor.valid())
        assertFalse(workspace.root.exists())
    }

    @Test
    fun diagnosticPublicationFailureMapsIoAndDeletesWorkspace() {
        val source = Files.writeString(Files.createTempFile("host-adapter", ".profile"), "{}")
        val workspace = workspace()
        assertTrue(workspace.rootScript.delete())
        workspace.rootScript.writeText("not-a-directory-" + ROOT_SCRIPT_HANDOFF_FILE_NAME)
        val adapter = adapter(environment = { environment(workspace) })
        val model = requestFor(source)
        val result = adapter.materialize(model, identity) {
            listOf(
                OpenedRunInputArtifact(
                    model.artifacts.single(),
                    openForStaging(source),
                ),
            )
        }
        assertEquals(RunHostCode.IO_FAILURE, rejectedCode(result))
        assertFalse(workspace.root.exists())
    }

    @Test
    fun flowFactoryFailureMapsIoFailureAndDeletesPublishedWorkspace() {
        val source = Files.writeString(Files.createTempFile("host-adapter", ".profile"), "{}")
        val workspace = workspace()
        val adapter = adapter(
            environment = { environment(workspace) },
            runtime = { runtime(createFlow = { error("native constructor failure") }) },
        )
        val model = requestFor(source)
        val result = adapter.materialize(model, identity) {
            listOf(
                OpenedRunInputArtifact(
                    model.artifacts.single(),
                    openForStaging(source),
                ),
            )
        }

        assertEquals(RunHostCode.IO_FAILURE, rejectedCode(result))
        assertFalse(workspace.root.exists())
    }

    @Test
    fun productCatalogInvariantMapsInternalAndDeletesWorkspace() {
        val profile = Files.writeString(Files.createTempFile("host-adapter", ".profile"), "{}")
        val manager = Files.writeString(Files.createTempFile("host-adapter", ".manager"), "manager")
        val workspace = workspace()
        val model = productRequestFor(profile, manager)
        val managerDescriptor = openForStaging(manager)
        val adapter = adapter(
            environment = { environment(workspace) },
            runtime = { runtime(runCapability = RunCapability.PRODUCT_ROOT) },
        )

        val result = adapter.materialize(model, identity) {
            listOf(OpenedRunInputArtifact(model.artifacts.last(), managerDescriptor))
        }

        assertEquals(RunHostCode.INTERNAL_CONTRACT_FAILURE, rejectedCode(result))
        assertFalse(managerDescriptor.fileDescriptor.valid())
        assertFalse(workspace.root.exists())
    }

    @Test
    fun productPublicationFailureMapsIoAndDeletesWorkspace() {
        val profile = Files.writeString(Files.createTempFile("host-adapter", ".profile"), "{}")
        val manager = Files.writeString(Files.createTempFile("host-adapter", ".manager"), "manager")
        val workspace = workspace()
        assertTrue(workspace.rootScript.delete())
        workspace.rootScript.writeText("not-a-directory")
        val model = productRequestFor(profile, manager)
        val profileDescriptor = openForStaging(profile)
        val managerDescriptor = openForStaging(manager)
        val adapter = adapter(
            environment = { environment(workspace) },
            runtime = { runtime(runCapability = RunCapability.PRODUCT_ROOT) },
        )

        val result = adapter.materialize(model, identity) {
            listOf(
                OpenedRunInputArtifact(model.artifacts.first(), profileDescriptor),
                OpenedRunInputArtifact(model.artifacts.last(), managerDescriptor),
            )
        }

        assertEquals(RunHostCode.IO_FAILURE, rejectedCode(result))
        assertFalse(profileDescriptor.fileDescriptor.valid())
        assertFalse(managerDescriptor.fileDescriptor.valid())
        assertFalse(workspace.root.exists())
    }

    @Test
    fun successfulDiagnosticTransfersWorkspaceAndClosesStagedDescriptor() {
        val source = Files.writeString(Files.createTempFile("host-adapter", ".profile"), "{}")
        val workspace = workspace()
        val owned = openForStaging(source)
        val adapter = adapter(environment = { environment(workspace) })
        val model = requestFor(source)
        val result = adapter.materialize(model, identity) {
            listOf(OpenedRunInputArtifact(model.artifacts.single(), owned))
        }

        assertTrue(result.toString(), result is RunHostCandidateResult.Prepared)
        assertFalse(owned.fileDescriptor.valid())
        val candidate = (result as RunHostCandidateResult.Prepared).candidate
        assertEquals(identity, candidate.identity)
        assertEquals(RunCapability.DIAGNOSTIC_ONLY, candidate.reportContext.runCapability)
        assertTrue(candidate.workspace.deleteAfterTerminal())
        assertFalse(workspace.root.exists())
    }

    private fun adapter(
        environment: () -> AndroidRunHostEnvironment? = { environment(workspace()) },
        runtime: (String) -> AndroidRunHostRuntime? = { runtime() },
        processMatches: (ExecutionDomain, RunHostRole, Int) -> Boolean = { _, _, _ -> true },
    ): AndroidRunHostPreparationAdapter = AndroidRunHostPreparationAdapter(
        role = RunHostRole.APP,
        hostEnvironment = environment,
        resolveRuntime = runtime,
        processMatches = processMatches,
    )

    private fun runtime(
        runCapability: RunCapability = RunCapability.DIAGNOSTIC_ONLY,
        availability: () -> BackendAvailability = { BackendAvailability.Ready() },
        createFlow: () -> Flow = { NoopFlow() },
    ): AndroidRunHostRuntime = AndroidRunHostRuntime(
        descriptor = BackendDescriptor(
            key = "diagnose",
            selectionPolicy = BackendSelectionPolicy.HIDDEN,
            abortPolicy = AbortPolicy.SAFE_DURING_RUN,
            executionDomain = ExecutionDomain.APP,
            runCapability = runCapability,
        ),
        availability = availability,
        createFlow = createFlow,
    )

    private fun environment(workspace: AndroidRunWorkspace): AndroidRunHostEnvironment =
        AndroidRunHostEnvironment(10_000) { _, _ -> workspace }

    private fun workspace(): AndroidRunWorkspace {
        val root = Files.createTempDirectory("host-adapter-workspace").toFile()
        return AndroidRunWorkspace(
            root = root,
            stagedInputs = root.resolve("input").apply { mkdir() },
            executables = root.resolve("executable").apply { mkdir() },
            rootScript = root.resolve("root-script").apply { mkdir() },
            persistentStateDirectory = root.resolve("state").apply { mkdir() },
        )
    }

    private fun request(): RunHostRequestModel = RunHostRequestModel(
        runId = identity.runId,
        generation = identity.generation,
        backendKey = "diagnose",
        profileArtifactId = "profile",
        payloadItems = emptyList(),
        finishItems = emptyList(),
        artifacts = listOf(
            RunHostArtifactRequest("profile", RunHostArtifactKind.PROFILE, 2L, "0".repeat(64)),
        ),
    )

    private fun requestFor(path: java.nio.file.Path): RunHostRequestModel {
        return request().copy(artifacts = listOf(artifactRequestFor(path, "profile", RunHostArtifactKind.PROFILE)))
    }

    private fun productRequestFor(
        profile: java.nio.file.Path,
        manager: java.nio.file.Path,
    ): RunHostRequestModel = RunHostRequestModel(
        runId = identity.runId,
        generation = identity.generation,
        backendKey = "diagnose",
        profileArtifactId = "profile",
        payloadItems = listOf(
            RunHostPayloadItemRequest(
                itemId = "payload-root",
                kind = RunHostPayloadKind.ROOT_MANAGER,
                required = true,
                artifactId = "root-manager-executable",
                args = listOf("late-load"),
                acceptance = RunHostPayloadAcceptance.DAEMON_ACK,
            ),
        ),
        finishItems = emptyList(),
        artifacts = listOf(
            artifactRequestFor(profile, "profile", RunHostArtifactKind.PROFILE),
            artifactRequestFor(manager, "root-manager-executable", RunHostArtifactKind.EXECUTABLE),
        ),
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

    private fun openForStaging(path: java.nio.file.Path): ParcelFileDescriptor {
        val descriptor = Os.open(path.toString(), OsConstants.O_RDONLY or OsConstants.O_CLOEXEC, 0)
        return try {
            ParcelFileDescriptor.dup(descriptor)
        } finally {
            Os.close(descriptor)
        }
    }

    private fun rejectedCode(result: RunHostCandidateResult): RunHostCode =
        (result as RunHostCandidateResult.Rejected).code

    private class NoopFlow : Flow {
        override suspend fun run(input: BackendFlowInput): RunReport = error("not started by preparation")

        override fun stop() = Unit
    }
}

/** Robolectric's host fstat omits the Unix type bits; device staging tests retain the physical check. */
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
            val size = FileInputStream(descriptor).channel.size()
            return StructStat(
                stat.st_dev,
                stat.st_ino,
                stat.st_mode or OsConstants.S_IFREG,
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
