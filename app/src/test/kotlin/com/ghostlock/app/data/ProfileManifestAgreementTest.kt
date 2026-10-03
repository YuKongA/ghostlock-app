package com.ghostlock.app.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * A2-3c-3 three-end manifest agreement (Kotlin leg).
 *
 * The native owner Schema is the single source of truth for the wire
 * (section, key) universe; it is exported to `profile-manifest.tsv` (see
 * `src/core/tests/profile_manifest_test.cpp`). This test fails if the Kotlin
 * document writer drifts from that manifest in either direction, so a key
 * added to only one side cannot reach production.
 */
class ProfileManifestAgreementTest {
    @Test
    fun kotlinWireKeysMatchTheNativeOwnerManifest() {
        val manifest = checkNotNull(
            javaClass.classLoader?.getResourceAsStream("profile-manifest.tsv"),
        ).bufferedReader().use { it.readText() }

        val manifestKeys = manifest.lineSequence()
            .filter { it.isNotBlank() && !it.startsWith("#") }
            .map { line ->
                val parts = line.split('\t')
                assertEquals("manifest line needs 5 tab-separated columns: $line", 5, parts.size)
                parts[1] to parts[2]
            }
            .toSet()

        assertTrue("manifest fixture is empty", manifestKeys.isNotEmpty())
        assertEquals(
            "Kotlin wire keys drifted from the native owner manifest",
            manifestKeys,
            NativeProfileDocument.declaredWireKeys(),
        )
    }
}
