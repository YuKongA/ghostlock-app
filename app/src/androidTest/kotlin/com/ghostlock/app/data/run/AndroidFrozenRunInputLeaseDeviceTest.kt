package com.ghostlock.app.data.run

import android.os.ParcelFileDescriptor
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.ghostlock.app.domain.finish.FinishDefinition
import com.ghostlock.app.domain.payload.PayloadDefinition
import com.ghostlock.app.domain.run.FrozenRunInput
import com.ghostlock.app.domain.run.ProfileSnapshot
import com.ghostlock.app.domain.run.RunInputArtifactKind
import java.io.File
import java.nio.file.Files
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Test
import org.junit.runner.RunWith

/** Device gate for same-open PFD duplication and idempotent owned-source cleanup. */
@RunWith(AndroidJUnit4::class)
class AndroidFrozenRunInputLeaseDeviceTest {
    /** Path replacement cannot change duplicated bytes, and a request duplicate survives master closure. */
    @Test
    fun duplicateRetainsOpenedObjectAcrossReplacementAndLeaseClose() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val directory = File(context.cacheDir, "lease-device-${System.nanoTime()}").apply { mkdir() }
        val sourceFile = File(directory, "profile.json").apply { writeText("original") }
        val source = OpenedRunInputSource.open(
            sourceFile,
            "profile",
            RunInputArtifactKind.PROFILE,
            ownedSource = false,
        )
        val lease = AndroidFrozenRunInputLease(input(source), listOf(source))
        val requestDuplicate = lease.duplicate(source.artifact.sourceToken)
        Files.move(sourceFile.toPath(), directory.toPath().resolve("opened-profile.json"))
        sourceFile.writeText("replacement")

        lease.close()
        lease.close()

        val text = ParcelFileDescriptor.AutoCloseInputStream(requestDuplicate).use { input ->
            input.readBytes().toString(Charsets.UTF_8)
        }
        assertEquals("original", text)
        directory.deleteRecursively()
    }

    /** Closing an owned-source lease removes its source once and remains idempotent. */
    @Test
    fun closeDeletesOwnedSourceExactlyOnce() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val directory = File(context.cacheDir, "lease-owned-${System.nanoTime()}").apply { mkdir() }
        val sourceFile = File(directory, "profile.json").apply { writeText("owned") }
        val source = OpenedRunInputSource.open(
            sourceFile,
            "profile",
            RunInputArtifactKind.PROFILE,
            ownedSource = true,
        )
        val lease = AndroidFrozenRunInputLease(input(source), listOf(source))

        lease.close()
        lease.close()

        assertFalse(sourceFile.exists())
        directory.deleteRecursively()
    }

    /** Returns the neutral input paired with one exact opened profile source. */
    private fun input(source: OpenedRunInputSource): FrozenRunInput = FrozenRunInput(
        runId = "0123456789abcdef0123456789abcdef",
        generation = 1L,
        backendKey = "fixture",
        profile = ProfileSnapshot("profile", 3, source.artifact.sha256),
        payload = PayloadDefinition(items = emptyList()),
        finish = FinishDefinition(),
        inputArtifacts = listOf(source.artifact),
    )
}
