package com.ghostlock.app.data.rootscript

import android.system.Os
import android.system.OsConstants
import java.nio.file.Files
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

/** Ordinary-file object and byte identity checks. */
@RunWith(RobolectricTestRunner::class)
class PublishedArtifactAccessTest {
    @Test
    fun detectsByteReplacementAndSymlinkReplacement() {
        val directory = Files.createTempDirectory("published-artifact")
        val file = directory.resolve("artifact")
        Files.writeString(file, "one")
        val identity = PublishedArtifactAccess.capture(file.toFile())
        assertTrue(PublishedArtifactAccess.matches(identity, file.toFile()))

        Files.writeString(file, "two")
        assertFalse(PublishedArtifactAccess.matches(identity, file.toFile()))

        Files.delete(file)
        val target = directory.resolve("target")
        Files.writeString(target, "one")
        Files.createSymbolicLink(file, target.fileName)
        assertFalse(PublishedArtifactAccess.matches(identity, file.toFile()))
        assertFalse(PublishedArtifactAccess.matchesObject(identity, file.toFile()))
    }

    @Test
    fun rejectsIdenticalBytesPublishedAtAnotherInode() {
        val directory = Files.createTempDirectory("published-artifact-inode")
        val file = directory.resolve("artifact")
        Files.writeString(file, "same-bytes")
        val identity = PublishedArtifactAccess.capture(file.toFile())
        val original = directory.resolve("original")
        Files.move(file, original)
        Files.writeString(file, "same-bytes")

        assertFalse(PublishedArtifactAccess.matches(identity, file.toFile()))
        assertFalse(PublishedArtifactAccess.matchesObject(identity, file.toFile()))
        assertTrue(PublishedArtifactAccess.matches(identity, original.toFile()))
    }

    @Test
    fun mutableResultReadsOnlyTheCapturedObject() {
        val directory = Files.createTempDirectory("published-result")
        val file = directory.resolve("result.glr1")
        Files.write(file, byteArrayOf())
        val identity = PublishedArtifactAccess.capture(file.toFile())
        val bytes = "GLR1\ncomplete=1\n".toByteArray(Charsets.US_ASCII)
        Files.write(file, bytes)

        assertTrue(PublishedArtifactAccess.matchesObject(identity, file.toFile()))
        val complete = PublishedArtifactAccess.readSameObject(file.toFile(), identity, 1024)
        assertTrue(complete is SameObjectRead.Complete)
        assertArrayEquals(bytes, (complete as SameObjectRead.Complete).bytes)

        Files.delete(file)
        Files.write(file, bytes)
        assertFalse(PublishedArtifactAccess.matchesObject(identity, file.toFile()))
        assertTrue(
            PublishedArtifactAccess.readSameObject(file.toFile(), identity, 1024) ===
                SameObjectRead.IdentityMismatch,
        )
    }

    /** Production metadata rejects a non-regular opened object even when the current path is a regular file. */
    @Test
    fun productionMetadataDoesNotReplaceDescriptorFactsWithPathFacts() {
        val directory = Files.createTempDirectory("published-non-regular")
        val file = directory.resolve("regular-path")
        Files.write(file, byteArrayOf(1))
        val directoryStat = Os.stat(directory.toString())
        assertFalse((directoryStat.st_mode and OsConstants.S_IFMT) == OsConstants.S_IFREG)

        val metadata = PublishedArtifactAccess.metadata(
            file = file.toFile(),
            stat = directoryStat,
            allowHostPathFallback = false,
        )

        assertFalse(metadata.regular)
    }
}
