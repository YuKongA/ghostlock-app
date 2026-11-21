package com.ghostlock.app.data.runhost.wire

import android.os.ParcelFileDescriptor
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
import java.nio.file.Files
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertThrows
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

/** Neutral frozen-input to AIDL mapping and duplicate cleanup tests. */
@RunWith(RobolectricTestRunner::class)
class RunHostRequestWireCodecTest {
    /** Every stable field and integer value maps without an intermediate data DTO. */
    @Test
    fun mapsExactRequestAndDescriptorDuplicates() {
        val directory = Files.createTempDirectory("wire-codec")
        val profile = Files.writeString(directory.resolve("profile"), "{}")
        val manager = Files.writeString(directory.resolve("manager"), "manager")
        val frozen = frozenInput()
        val sources = mapOf("profile-token" to profile, "manager-token" to manager)

        val request = frozen.toWireRequest { token ->
            ParcelFileDescriptor.open(
                sources.getValue(token).toFile(),
                ParcelFileDescriptor.MODE_READ_ONLY,
            )
        }
        try {
            assertEquals(frozen.runId, request.runId)
            assertEquals(frozen.generation, request.generation)
            assertEquals(frozen.backendKey, request.backendKey)
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
            request.artifacts.forEach { artifact -> artifact.fd.close() }
        }
    }

    /** A later duplicate failure closes every descriptor already transferred into the partial request. */
    @Test
    fun partialDuplicateFailureClosesEarlierDescriptors() {
        val source = Files.writeString(Files.createTempFile("wire-partial", ".bin"), "source")
        var first: ParcelFileDescriptor? = null
        var calls = 0

        assertThrows(IllegalStateException::class.java) {
            frozenInput().toWireRequest {
                calls += 1
                if (calls == 2) error("duplicate failure")
                ParcelFileDescriptor.open(source.toFile(), ParcelFileDescriptor.MODE_READ_ONLY).also {
                    first = it
                }
            }
        }

        assertEquals(2, calls)
        assertFalse(requireNotNull(first).fileDescriptor.valid())
    }

    /** Returns one complete frozen request with profile, manager, Payload, and reboot Finish. */
    private fun frozenInput(): FrozenRunInput = FrozenRunInput(
        runId = "0123456789abcdef0123456789abcdef",
        generation = 3L,
        backendKey = "fixture",
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
    }
}
