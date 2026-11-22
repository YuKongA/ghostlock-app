package com.ghostlock.app.data.runhost.wire

import android.os.ParcelFileDescriptor
import android.system.OsConstants
import com.ghostlock.app.core.runhost.RunHostArtifactKind
import com.ghostlock.app.core.runhost.RunHostArtifactRequest
import com.ghostlock.app.core.runhost.RunHostRequestModel
import com.ghostlock.app.data.runhost.host.RegularFileFstatShadow
import com.ghostlock.app.domain.finish.FinishDefinition
import com.ghostlock.app.domain.finish.FinishItemDefinition
import com.ghostlock.app.domain.finish.FinishKind
import com.ghostlock.app.domain.finish.FinishSeverity
import com.ghostlock.app.domain.payload.PayloadAcceptance
import com.ghostlock.app.domain.payload.PayloadDefinition
import com.ghostlock.app.domain.payload.PayloadKind
import com.ghostlock.app.domain.payload.PayloadSlot
import com.ghostlock.app.domain.run.FrozenArtifactInvocation
import com.ghostlock.app.domain.run.FrozenRunInput
import com.ghostlock.app.domain.run.ProfileSnapshot
import com.ghostlock.app.domain.run.RunInputArtifact
import com.ghostlock.app.domain.run.RunInputArtifactKind
import com.ghostlock.app.run.RunHostRequest
import com.ghostlock.app.run.RunInputArtifactParcel
import java.io.IOException
import java.nio.file.Files
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

/** Neutral frozen-input to AIDL mapping and duplicate cleanup tests. */
@RunWith(RobolectricTestRunner::class)
@Config(shadows = [RegularFileFstatShadow::class])
class RunHostRequestWireCodecTest {
    /** Every stable field and integer value maps without an intermediate data DTO. */
    @Test
    fun mapsExactRequestAndDescriptorDuplicates() {
        val directory = Files.createTempDirectory("wire-codec")
        val profile = Files.writeString(directory.resolve("profile"), "{}")
        val manager = Files.writeString(directory.resolve("manager"), "manager")
        val plugin = Files.writeString(directory.resolve("plugin.glplugin.zip"), "plugin")
        val frozen = frozenInput()
        val sources = mapOf("profile-token" to profile, "manager-token" to manager)

        val request = frozen.toWireRequest(
            duplicatePluginSource = {
                ParcelFileDescriptor.open(plugin.toFile(), ParcelFileDescriptor.MODE_READ_ONLY)
            },
            duplicateSource = { token ->
                ParcelFileDescriptor.open(
                    sources.getValue(token).toFile(),
                    ParcelFileDescriptor.MODE_READ_ONLY,
                )
            },
        )
        try {
            assertEquals(frozen.runId, request.runId)
            assertEquals(frozen.generation, request.generation)
            assertEquals(frozen.backendKey, request.backendKey)
            assertEquals(frozen.pluginContentSha256, request.pluginContentSha256)
            assertEquals("profile", request.profileArtifactId)
            assertEquals(listOf(1, 3), request.artifacts.map { artifact -> artifact.kind })
            assertEquals(3, request.payloadItems.single().kind)
            assertEquals(2, request.payloadItems.single().acceptanceKind)
            assertEquals("root-manager", request.payloadItems.single().artifactId)
            assertEquals(listOf("late-load", "--kmi", "android13-5.15"), request.payloadItems.single().args)
            assertEquals(3, request.finishItems.single().kind)
            assertEquals(2, request.finishItems.single().severity)
            assertEquals(true, request.finishItems.single().alwaysRun)
        } finally {
            request.pluginArchiveFd.close()
            request.artifacts.forEach { artifact -> artifact.fd.close() }
        }
    }

    /** A later duplicate failure closes every descriptor already transferred into the partial request. */
    @Test
    fun partialDuplicateFailureClosesEarlierDescriptors() {
        val source = Files.writeString(Files.createTempFile("wire-partial", ".bin"), "source")
        var first: ParcelFileDescriptor? = null
        var plugin: ParcelFileDescriptor? = null
        var calls = 0

        assertThrows(IllegalStateException::class.java) {
            frozenInput().toWireRequest(
                duplicatePluginSource = {
                    ParcelFileDescriptor.open(source.toFile(), ParcelFileDescriptor.MODE_READ_ONLY).also {
                        plugin = it
                    }
                },
                duplicateSource = {
                    calls += 1
                    if (calls == 2) error("duplicate failure")
                    ParcelFileDescriptor.open(source.toFile(), ParcelFileDescriptor.MODE_READ_ONLY).also {
                        first = it
                    }
                },
            )
        }

        assertEquals(2, calls)
        assertFalse(requireNotNull(plugin).fileDescriptor.valid())
        assertFalse(requireNotNull(first).fileDescriptor.valid())
    }

    /** Validated duplicates move once to staging/loader and are not reclosed by the emptied holder. */
    @Test
    fun validatedDescriptorBatchTransfersEachOwnerExactlyOnce() {
        val source = Files.writeString(Files.createTempFile("wire-owner", ".bin"), "source")
        val plugin = ParcelFileDescriptor.open(source.toFile(), ParcelFileDescriptor.MODE_READ_ONLY)
        val artifact = ParcelFileDescriptor.open(source.toFile(), ParcelFileDescriptor.MODE_READ_ONLY)
        val model = descriptorModel(1)
        val request = descriptorRequest(plugin, listOf(artifact))

        val holder = request.duplicateDescriptors(model)
        val transferredArtifacts = holder.takeArtifacts()
        val transferredPlugin = holder.takePluginArchive()
        holder.close()

        assertTrue(plugin.fileDescriptor.valid())
        assertTrue(artifact.fileDescriptor.valid())
        assertTrue(transferredPlugin.fileDescriptor.valid())
        assertTrue(transferredArtifacts.single().descriptor.fileDescriptor.valid())
        assertThrows(IllegalStateException::class.java) { holder.takeArtifacts() }
        assertThrows(IllegalStateException::class.java) { holder.takePluginArchive() }

        transferredArtifacts.single().descriptor.close()
        transferredPlugin.close()
        request.closeDecodedDescriptors()
        assertFalse(plugin.fileDescriptor.valid())
        assertFalse(artifact.fileDescriptor.valid())
    }

    /** Null and already-closed archive descriptors fail before any RunHost duplicate is retained. */
    @Test
    fun archiveDescriptorPreflightRejectsMissingOrClosedPfd() {
        val source = Files.writeString(Files.createTempFile("wire-invalid-plugin", ".bin"), "source")

        assertInvalidPluginDescriptor("null", null)

        val closed = ParcelFileDescriptor.open(source.toFile(), ParcelFileDescriptor.MODE_READ_ONLY)
        closed.close()
        assertInvalidPluginDescriptor("closed", closed)
    }

    /** Exact F_GETFL/fstat facts reject writable, O_PATH, and non-regular archive sources. */
    @Test
    fun archiveDescriptorKernelFactsRequireReadOnlyRegularFile() {
        requireReadOnlyRegularPluginArchiveFacts(OsConstants.O_RDONLY, OsConstants.S_IFREG)
        assertThrows(IllegalArgumentException::class.java) {
            requireReadOnlyRegularPluginArchiveFacts(OsConstants.O_RDWR, OsConstants.S_IFREG)
        }
        assertThrows(IllegalArgumentException::class.java) {
            requireReadOnlyRegularPluginArchiveFacts(
                OsConstants.O_RDONLY or LINUX_O_PATH,
                OsConstants.S_IFREG,
            )
        }
        assertThrows(IllegalArgumentException::class.java) {
            requireReadOnlyRegularPluginArchiveFacts(OsConstants.O_RDONLY, OsConstants.S_IFIFO)
        }
    }

    /** A later ordinary dup failure closes partial owned duplicates without stealing source ownership. */
    @Test
    fun partialRunHostDupFailureLeavesDecodeSourcesForEndpointCleanup() {
        val source = Files.writeString(Files.createTempFile("wire-host-partial", ".bin"), "source")
        val plugin = ParcelFileDescriptor.open(source.toFile(), ParcelFileDescriptor.MODE_READ_ONLY)
        val first = ParcelFileDescriptor.open(source.toFile(), ParcelFileDescriptor.MODE_READ_ONLY)
        val closedSecond = ParcelFileDescriptor.open(source.toFile(), ParcelFileDescriptor.MODE_READ_ONLY)
        closedSecond.close()
        val request = descriptorRequest(plugin, listOf(first, closedSecond))

        assertThrows(IOException::class.java) {
            request.duplicateDescriptors(descriptorModel(2))
        }
        assertTrue(plugin.fileDescriptor.valid())
        assertTrue(first.fileDescriptor.valid())

        request.closeDecodedDescriptors()
        assertFalse(plugin.fileDescriptor.valid())
        assertFalse(first.fileDescriptor.valid())
    }

    private fun assertInvalidPluginDescriptor(label: String, plugin: ParcelFileDescriptor?) {
        val source = Files.writeString(Files.createTempFile("wire-invalid-artifact", ".bin"), "source")
        val artifact = ParcelFileDescriptor.open(source.toFile(), ParcelFileDescriptor.MODE_READ_ONLY)
        val request = descriptorRequest(plugin, listOf(artifact))
        var unexpectedHolder: OwnedRunHostRequestDescriptors? = null
        try {
            val failure = runCatching {
                unexpectedHolder = request.duplicateDescriptors(descriptorModel(1))
            }.exceptionOrNull()
            assertTrue("expected invalid $label archive PFD, got $failure", failure is IllegalArgumentException)
            assertTrue(artifact.fileDescriptor.valid())
        } finally {
            unexpectedHolder?.close()
            request.closeDecodedDescriptors()
        }
    }

    private fun descriptorModel(artifactCount: Int): RunHostRequestModel = RunHostRequestModel(
        runId = "1".repeat(32),
        generation = 1L,
        backendKey = "diagnose",
        pluginContentSha256 = "a".repeat(64),
        profileArtifactId = "artifact-0",
        payloadItems = emptyList(),
        finishItems = emptyList(),
        artifacts = List(artifactCount) { index ->
            RunHostArtifactRequest(
                logicalId = "artifact-$index",
                kind = RunHostArtifactKind.PROFILE,
                byteCount = 6L,
                sha256 = DIGEST,
            )
        },
    )

    private fun descriptorRequest(
        plugin: ParcelFileDescriptor?,
        artifacts: List<ParcelFileDescriptor>,
    ): RunHostRequest = RunHostRequest().also { request ->
        request.pluginArchiveFd = plugin
        request.artifacts = artifacts.mapIndexed { index, descriptor ->
            RunInputArtifactParcel().also { artifact ->
                artifact.logicalId = "artifact-$index"
                artifact.kind = RunHostArtifactKind.PROFILE.wireValue
                artifact.byteCount = 6L
                artifact.sha256 = DIGEST
                artifact.fd = descriptor
            }
        }
    }

    /** Returns one complete frozen request with profile, manager, Payload, and reboot Finish. */
    private fun frozenInput(): FrozenRunInput = FrozenRunInput(
        runId = "0123456789abcdef0123456789abcdef",
        generation = 3L,
        backendKey = "fixture",
        pluginContentSha256 = "a".repeat(64),
        pluginSourceToken = "f".repeat(32),
        profile = ProfileSnapshot("profile", 3, DIGEST),
        payload = PayloadDefinition(
            items = listOf(
                PayloadSlot(
                    id = "payload-root",
                    kind = PayloadKind.RootManager,
                    required = true,
                    acceptance = PayloadAcceptance.DaemonAck,
                ),
            ),
        ),
        finish = FinishDefinition(
            items = listOf(
                FinishItemDefinition(
                    id = "finish-reboot",
                    kind = FinishKind.Reboot,
                    severity = FinishSeverity.SafetyCritical,
                    alwaysRun = true,
                ),
            ),
        ),
        inputArtifacts = listOf(
            RunInputArtifact("profile", RunInputArtifactKind.PROFILE, 2L, DIGEST, "profile-token"),
            RunInputArtifact(
                "root-manager",
                RunInputArtifactKind.EXECUTABLE,
                7L,
                DIGEST,
                "manager-token",
            ),
        ),
        artifactInvocations = listOf(
            FrozenArtifactInvocation(
                "payload-root",
                "root-manager",
                listOf("late-load", "--kmi", "android13-5.15"),
            ),
        ),
    )

    private companion object {
        val DIGEST: String = "0".repeat(64)
        const val LINUX_O_PATH = 0x200000
    }
}
