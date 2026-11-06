package com.ghostlock.app.data.run

import com.ghostlock.app.backend.declarations.BackendCarrierArtifact
import java.nio.file.Files
import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment

/** Exact declaration-only packaged carrier resolution checks. */
@RunWith(RobolectricTestRunner::class)
class PackagedBackendArtifactResolverTest {
    @Test
    fun opensDeclaredNativeLibraryWithoutOwningDeletion() {
        val context = RuntimeEnvironment.getApplication()
        val directory = Files.createTempDirectory("packaged-carrier")
        val carrier = directory.resolve("libfixture.so").toFile().apply { writeText("carrier") }
        context.applicationInfo.nativeLibraryDir = directory.toString()
        val resolver = PackagedBackendArtifactResolver(context) { key ->
            BackendCarrierArtifact("carrier", "libfixture.so").takeIf { key == "fixture" }
        }
        val opened = resolver.resolve("fixture")
        try {
            assertEquals("carrier", opened.artifact.logicalId)
            assertEquals(carrier.length(), opened.artifact.byteCount)
        } finally {
            opened.master.close()
        }
        assertTrue(carrier.exists())
    }

    @Test
    fun rejectsLinkedCarrierObject() {
        val context = RuntimeEnvironment.getApplication()
        val directory = Files.createTempDirectory("linked-carrier")
        val target = Files.writeString(directory.resolve("target.so"), "carrier")
        Files.createSymbolicLink(directory.resolve("libfixture.so"), target.fileName)
        context.applicationInfo.nativeLibraryDir = directory.toString()
        val resolver = PackagedBackendArtifactResolver(context) {
            BackendCarrierArtifact("carrier", "libfixture.so")
        }
        assertThrows(Exception::class.java) { resolver.resolve("fixture") }
    }
}
