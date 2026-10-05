package com.ghostlock.app.data

import com.ghostlock.app.data.profile.NativeProfileGlkv3Adapter
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

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
        val adapterTypes = NativeProfileGlkv3Adapter.declaredTypes()
            .mapValues { (_, type) -> type.manifestName }
        assertEquals(
            "Kotlin GLKv3 path/type table drifted from the native GLKv3 manifest",
            manifestTypes,
            adapterTypes,
        )
    }
}
