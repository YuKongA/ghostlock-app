package com.ghostlock.app.data.run

import android.os.ParcelFileDescriptor
import com.ghostlock.app.core.run.FrozenRunInputLease
import com.ghostlock.app.core.run.OpenedRunInputSource
import com.ghostlock.app.data.run.copyLogPrefix
import com.ghostlock.app.domain.run.LogFilePrefix
import com.ghostlock.app.domain.finish.FinishDefinition
import com.ghostlock.app.domain.payload.PayloadDefinition
import com.ghostlock.app.domain.payload.PayloadKind
import com.ghostlock.app.domain.payload.PayloadSlot
import com.ghostlock.app.domain.run.FrozenArtifactInvocation
import com.ghostlock.app.domain.run.FrozenRunInput
import com.ghostlock.app.domain.run.ProfileSnapshot
import com.ghostlock.app.domain.run.RunInputArtifactKind
import java.nio.file.Files
import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

/** Lease-to-wire ownership and normalized RootManager request checks. */
@RunWith(RobolectricTestRunner::class)
class AndroidRunSessionAdapterTest {
    /** An active barrier's byte length freezes the shared prefix despite later append. */
    @Test
    fun exactPrefixCopyDoesNotIncludeConcurrentAppend() {
        val source = Files.createTempFile("log-prefix-source", ".txt")
        val target = Files.createTempFile("log-prefix-target", ".txt")
        Files.writeString(source, "first\n")
        val prefix = LogFilePrefix(source.toString(), Files.size(source))
        Files.writeString(source, "first\nsecond\n")
        copyLogPrefix(prefix, target.toFile())
        assertEquals("first\n", Files.readString(target))
    }

    @Test
    fun requestUsesLeaseDuplicatesAndFrozenManagerArgv() {
        val directory = Files.createTempDirectory("lease-wire")
        val profile = OpenedRunInputSource.open(
            Files.writeString(directory.resolve("profile.json"), "{}").toFile(),
            "profile",
            RunInputArtifactKind.PROFILE,
        )
        val manager = OpenedRunInputSource.open(
            Files.writeString(directory.resolve("manager.so"), "manager").toFile(),
            "root-manager-executable",
            RunInputArtifactKind.EXECUTABLE,
        )
        val args = listOf("late-load", "--allow-shell", "--kmi", "android13-5.15")
        val lease = FrozenRunInputLease(
            FrozenRunInput(
                runId = "0123456789abcdef0123456789abcdef",
                generation = 1L,
                backendKey = "fixture",
                profile = ProfileSnapshot("profile", 3, profile.artifact.sha256),
                payload = PayloadDefinition(
                    items = listOf(PayloadSlot("payload-root", PayloadKind.RootManager)),
                ),
                finish = FinishDefinition(),
                inputArtifacts = listOf(profile.artifact, manager.artifact),
                artifactInvocations = listOf(
                    FrozenArtifactInvocation("payload-root", "root-manager-executable", args),
                ),
            ),
            listOf(profile, manager),
        )
        val request = lease.toWireRequest()
        lease.close()
        try {
            assertEquals(2, request.artifacts.size)
            assertEquals(3, request.payloadItems.single().kind)
            assertEquals("root-manager-executable", request.payloadItems.single().artifactId)
            assertEquals(args, request.payloadItems.single().args)
            val bytes = ParcelFileDescriptor.AutoCloseInputStream(
                ParcelFileDescriptor.dup(request.artifacts[1].fd.fileDescriptor),
            ).use { input -> input.readBytes() }
            assertEquals("manager", bytes.toString(Charsets.UTF_8))
        } finally {
            request.artifacts.forEach { artifact -> artifact.fd.close() }
        }
    }
}
