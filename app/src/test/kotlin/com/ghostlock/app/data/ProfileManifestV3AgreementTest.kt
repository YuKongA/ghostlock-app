package com.ghostlock.app.data

import com.ghostlock.app.data.profile.NativeProfileGlkv3Adapter
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File

/**
 * GLKv3 owner-qualified manifest agreement (Kotlin leg, S4 R2).
 *
 * The native GLKv3 FieldSpec lists are the single source of truth for the
 * (owner, path, wire, required, default, source, doc) manifest universe; they are
 * exported to `profile-manifest-v3.tsv` (see
 * src/core/tests/profile_manifest_v3_test.cpp). The Kotlin adapter parses that
 * same manifest as its runtime type map; this test fails if the app test
 * resource and the profile-core runtime resource drift in either the path set
 * or a wire type.
 */
class ProfileManifestV3AgreementTest {
    @Test
    fun kotlinAdapterPathsAndTypesMatchTheNativeGlkv3Manifest() {
        val manifest = checkNotNull(
            javaClass.classLoader?.getResourceAsStream("profile-manifest-v3.tsv"),
        ).bufferedReader().use { it.readText() }

        val manifestTypes = manifest.lineSequence()
            .filter { it.isNotBlank() && !it.startsWith("#") }
            .associate { line ->
                val parts = line.split('\t')
                assertEquals("manifest-v3 line needs 7 tab-separated columns: " + line, 7, parts.size)
                assertEquals("manifest-v3 fields are all optional: " + line, "0", parts[3])
                assertEquals("manifest-v3 owner is empty: " + line, true, parts[0].isNotBlank())
                parts[1] to parts[2]
            }

        assertTrue("manifest-v3 fixture is empty", manifestTypes.isNotEmpty())
        /* The manifest wire column may be a "|"-joined union for the dynamic
         * plugin paths, so the comparison is on the manifest spelling. */
        val adapterTypes = NativeProfileGlkv3Adapter.declaredTypeNames()
        assertEquals(
            "Kotlin GLKv3 path/type table drifted from the native GLKv3 manifest",
            manifestTypes,
            adapterTypes,
        )
    }

    /**
     * The exporter writes the SAME text to two destinations. Loading through the
     * classpath cannot tell them apart (only one of them wins), so this test
     * compares the two committed files through their REPOSITORY paths: the
     * check is deliberately non-self-referential and fails even when both
     * copies are consistently wrong in the same way the classpath-visible copy
     * would hide.
     */
    @Test
    fun theTwoCommittedManifestCopiesAreByteIdentical() {
        val root = repoRoot()
        val appCopy = File(root, "app/src/test/resources/profile-manifest-v3.tsv")
        val runtimeCopy = File(root, "profile-core/src/main/resources/profile-manifest-v3.tsv")
        assertTrue("missing app manifest copy: " + appCopy.path, appCopy.isFile)
        assertTrue("missing runtime manifest copy: " + runtimeCopy.path, runtimeCopy.isFile)
        assertArrayEquals(
            "profile-manifest-v3.tsv drifted between the app test resource and the " +
                "profile-core runtime resource (regenerate with make -C src profile-manifest-v3)",
            runtimeCopy.readBytes(),
            appCopy.readBytes(),
        )
    }

    private fun repoRoot(): File {
        var current = File(requireNotNull(System.getProperty("user.dir"))).canonicalFile
        repeat(5) {
            if (File(current, "app/src/test/resources/profile-manifest-v3.tsv").isFile) {
                return current
            }
            current = current.parentFile ?: error("cannot locate repository root")
        }
        error("cannot locate repository root")
    }
}
