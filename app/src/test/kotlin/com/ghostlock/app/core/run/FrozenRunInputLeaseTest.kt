package com.ghostlock.app.core.run

import android.os.ParcelFileDescriptor
import com.ghostlock.app.domain.finish.FinishDefinition
import com.ghostlock.app.domain.payload.PayloadDefinition
import com.ghostlock.app.domain.run.FrozenRunInput
import com.ghostlock.app.domain.run.ProfileSnapshot
import com.ghostlock.app.domain.run.RunInputArtifactKind
import java.nio.file.Files
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

/** Same-open-object and owned-source lifecycle checks for the freeze lease. */
@RunWith(RobolectricTestRunner::class)
class FrozenRunInputLeaseTest {
    @Test
    fun duplicateKeepsOpenedInodeAfterPathReplacement() {
        val directory = Files.createTempDirectory("frozen-run-lease")
        val sourcePath = directory.resolve("profile.json")
        Files.writeString(sourcePath, "original")
        val source = OpenedRunInputSource.open(
            sourcePath.toFile(),
            "profile",
            RunInputArtifactKind.PROFILE,
        )
        val lease = lease(source)
        Files.move(sourcePath, directory.resolve("opened-object.json"))
        Files.writeString(sourcePath, "replacement")

        val bytes = ParcelFileDescriptor.AutoCloseInputStream(
            lease.duplicate(source.artifact.sourceToken),
        ).use { input -> input.readBytes() }
        assertEquals("original", bytes.toString(Charsets.UTF_8))
        lease.close()
    }

    @Test
    fun closeIsIdempotentAndDeletesOnlyOwnedPrivateSource() {
        val source = Files.createTempFile("owned-run-input", ".json").toFile().apply {
            writeText("owned")
        }
        val opened = OpenedRunInputSource.open(
            source,
            "profile",
            RunInputArtifactKind.PROFILE,
            ownedSource = true,
        )
        val lease = lease(opened)
        lease.close()
        lease.close()
        assertFalse(source.exists())
    }

    private fun lease(source: OpenedRunInputSource): FrozenRunInputLease = FrozenRunInputLease(
        input = FrozenRunInput(
            runId = "0123456789abcdef0123456789abcdef",
            generation = 1L,
            backendKey = "fixture",
            profile = ProfileSnapshot("profile", 3, source.artifact.sha256),
            payload = PayloadDefinition(items = emptyList()),
            finish = FinishDefinition(),
            inputArtifacts = listOf(source.artifact),
        ),
        sources = listOf(source),
    )
}
