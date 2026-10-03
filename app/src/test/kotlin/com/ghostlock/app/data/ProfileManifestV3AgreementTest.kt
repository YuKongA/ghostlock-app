package com.ghostlock.app.data

import com.ghostlock.app.data.profile.NativeProfileGlkv3Adapter
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * GLKv3-3 three-end path -> type manifest agreement (Kotlin leg).
 *
 * The native GLKv3 FieldSpec lists are the single source of truth for the v3
 * (path, wire) universe; they are exported to
 * `profile-manifest-v3.tsv` (see src/core/tests/profile_manifest_v3_test.cpp).
 * This test fails if the Kotlin adapter table drifts from that manifest in
 * either the path set or a wire type, so a field added or retyped on only one
 * side cannot reach the GLKv3-4 production migration.
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
                val parts = line.split('	')
                assertEquals("manifest-v3 line needs 4 tab-separated columns: " + line, 4, parts.size)
                assertEquals("manifest-v3 fields are all optional: " + line, "0", parts[3])
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
