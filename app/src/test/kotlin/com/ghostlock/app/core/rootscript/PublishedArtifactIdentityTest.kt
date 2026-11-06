package com.ghostlock.app.core.rootscript

import java.nio.file.Files
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

/** Ordinary-file object and byte identity checks. */
@RunWith(RobolectricTestRunner::class)
class PublishedArtifactIdentityTest {
    @Test
    fun detectsByteReplacementAndSymlinkReplacement() {
        val directory = Files.createTempDirectory("published-artifact")
        val file = directory.resolve("artifact")
        Files.writeString(file, "one")
        val identity = PublishedArtifactIdentity.capture(file.toFile())
        assertTrue(identity.matches(file.toFile()))

        Files.writeString(file, "two")
        assertFalse(identity.matches(file.toFile()))

        Files.delete(file)
        val target = directory.resolve("target")
        Files.writeString(target, "one")
        Files.createSymbolicLink(file, target.fileName)
        assertFalse(identity.matches(file.toFile()))
        assertFalse(identity.matchesObject(file.toFile()))
    }

    @Test
    fun rejectsIdenticalBytesPublishedAtAnotherInode() {
        val directory = Files.createTempDirectory("published-artifact-inode")
        val file = directory.resolve("artifact")
        Files.writeString(file, "same-bytes")
        val identity = PublishedArtifactIdentity.capture(file.toFile())
        val original = directory.resolve("original")
        Files.move(file, original)
        Files.writeString(file, "same-bytes")

        assertFalse(identity.matches(file.toFile()))
        assertFalse(identity.matchesObject(file.toFile()))
        assertTrue(identity.matches(original.toFile()))
    }

    @Test
    fun mutableResultReadsOnlyTheCapturedObject() {
        val directory = Files.createTempDirectory("published-result")
        val file = directory.resolve("result.glr1")
        Files.write(file, byteArrayOf())
        val identity = PublishedArtifactIdentity.capture(file.toFile())
        val bytes = "GLR1\ncomplete=1\n".toByteArray(Charsets.US_ASCII)
        Files.write(file, bytes)

        assertTrue(identity.matchesObject(file.toFile()))
        assertArrayEquals(bytes, identity.readSameObject(file.toFile()))

        Files.delete(file)
        Files.write(file, bytes)
        assertFalse(identity.matchesObject(file.toFile()))
        assertNull(identity.readSameObject(file.toFile()))
    }
}
