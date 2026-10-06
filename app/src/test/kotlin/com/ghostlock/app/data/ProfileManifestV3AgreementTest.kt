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
    private fun manifestText(): String = checkNotNull(
        javaClass.classLoader?.getResourceAsStream("profile-manifest-v3.tsv"),
    ) { "missing profile-manifest-v3.tsv" }.bufferedReader().use { it.readText() }

    /*
     * COMMENTED OUT with the payload owner test below (user ruling 2026-10-05):
     * the row reader exists only to pin the payload rows.
     *
     * owner/path/wire/required of every non-comment row, keyed by path.
     */
    // private fun manifestRows(): List<List<String>> = manifestText().lineSequence()
    //     .filter { it.isNotBlank() && !it.startsWith("#") }
    //     .map { it.split('\t') }
    //     .toList()

    @Test
    fun kotlinAdapterPathsAndTypesMatchTheNativeGlkv3Manifest() {
        val manifestTypes = manifestText().lineSequence()
            .filter { it.isNotBlank() && !it.startsWith("#") }
            .associate { line ->
                val parts = line.split('\t')
                /* R2+ native export: owner, path, wire, required, default, source, doc, width.
                The width column is a hard validation input consumed by
                NativeProfileGlkv3Adapter (fail-closed parse). */
                assertEquals("manifest-v3 line needs 8 tab-separated columns: " + line, 8, parts.size)
                /* Required is a FLAG, not "always optional": a manifest may
                 * legitimately declare required fields (the payload owner briefly
                 * did, native 74db3594). Both values are legal; anything else is a
                 * malformed manifest. The old "all optional" invariant is retired. */
                assertTrue(
                    "manifest-v3 required column must be 0 or 1: " + line,
                    parts[3] == "0" || parts[3] == "1",
                )
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

    /*
     * COMMENTED OUT (user ruling 2026-10-05): payload engineering is paused, so
     * the payload owner is no longer declared in the manifest and these eight rows
     * must not be asserted. Restore together with the payload feature (and with
     * the native export).
     *
     * Batch B (native 74db3594): the payload owner is declared. The Kotlin side
     * has NO hand-written GLKv3 path table — this adapter PARSES the manifest —
     * so "Kotlin accepts payload.*" means exactly this assertion: the eight rows
     * are declared and typed, index placeholders included, in the SAME
     * angle-bracket convention the plugin owner already uses:
     *
     *   plugin   plugin.<id>.params.* / plugin.<id>.enabled         (placeholder <id>)
     *   payload  payload.ko.<i>.path / payload.ko.<i>.sha256          (placeholder <i>)
     *
     * The ko index lives UNDER `ko.` (native 1143485c): the bare `<i>.path` and
     * any foreign prefix are rejected fail-closed by the parser, so this row set
     * is the whole accepted spelling.
     *
     * Nothing here emits, stores or persists a payload value: that is batch (b).
     */
    // @Test
    // fun theManifestDeclaresThePayloadOwnerRows() {
    //     val rows = manifestRows()
    //         .filter { it[0] == "payload" }
    //         .associate { it[1] to (it[2] to it[3]) }
    //
    //     assertEquals(
    //         mapOf(
    //             "payload.tier" to ("str" to "1"),
    //             "payload.exec.command" to ("str" to "0"),
    //             "payload.exec.sha256" to ("str" to "0"),
    //             "payload.script.path" to ("str" to "0"),
    //             "payload.script.sha256" to ("str" to "0"),
    //             "payload.ko.count" to ("uint" to "0"),
    //             "payload.ko.<i>.path" to ("str" to "0"),
    //             "payload.ko.<i>.sha256" to ("str" to "0"),
    //         ),
    //         rows,
    //     )
    //     /* The adapter PARSES this manifest (no hand-written path table), so it
    //      * exposes exactly the same path/type rows. */
    //     assertEquals(
    //         rows.mapValues { it.value.first },
    //         NativeProfileGlkv3Adapter.declaredTypeNames().filterKeys { it.startsWith("payload") },
    //     )
    //     /* The owner column says payload for every one of them. */
    //     assertEquals(
    //         setOf("payload"),
    //         NativeProfileGlkv3Adapter.declaredOwners()
    //             .filterKeys { it.startsWith("payload") }
    //             .values
    //             .toSet(),
    //     )
    // }

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
