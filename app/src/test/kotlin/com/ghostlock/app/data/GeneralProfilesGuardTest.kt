package com.ghostlock.app.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File

/**
 * Batch 2 guards (design 2.5 / 2.8):
 *  1. the set of general-<label>.conf assets corresponds EXACTLY to the labels in
 *     lkm-kmi-manifest.tsv (no hand-copied list, no missing/extra file);
 *  2. every general profile defaults to cve_2026_43284 and carries a non-empty queue.
 *
 * All availability checks are scoped to the available { ... } block: the backend
 * owner section also contains cve_2026_43499 { ... } and would otherwise be a false hit.
 * The full requirements check (U10) is machine-exported in batch 4 from the manifest
 * required column - it is deliberately NOT asserted here.
 */
class GeneralProfilesGuardTest {

    private val assetsDir = File("src/main/assets/profile")
    private val manifestFile = File("src/test/resources/lkm-kmi-manifest.tsv")

    /** label<TAB>android_release<TAB>kmi<TAB>ko_filename, comments skipped. */

    /** label android14-6.1 -> 6.1-android14-general.conf (manifest derived). */
    private fun expectedName(label: String): String {
        val parts = label.split("-", limit = 2)
        return parts[1] + "-" + parts[0] + "-general.conf"
    }
    private fun labels(): List<String> {
        assertTrue("missing manifest " + manifestFile.absolutePath, manifestFile.isFile)
        val rows = manifestFile.readLines()
            .filter { it.isNotBlank() && !it.startsWith("#") }
            .map { line -> line.split('\t') }
        rows.forEach { parts ->
            assertEquals("manifest row needs 4 columns: " + parts.joinToString("|"), 4, parts.size)
        }
        return rows.map { it[0] }
    }

    /** The text inside the first available { ... } block, comments kept. */
    private fun availableBlock(text: String): String {
        val lines = text.lines()
        val start = lines.indexOfFirst { it.trim() == "available {" }
        assertTrue("no available block", start >= 0)
        var depth = 0
        val out = mutableListOf<String>()
        for (i in start until lines.size) {
            depth += lines[i].count { c -> c == '{' } - lines[i].count { c -> c == '}' }
            out += lines[i]
            if (depth == 0) break
        }
        return out.joinToString("\n")
    }

    /** Uncommented lines only, so a commented declaration never counts. */
    private fun activeLines(block: String): List<String> =
        block.lines().filter { it.isNotBlank() && !it.trim().startsWith("#") }

    @Test
    fun theGeneralProfileSetMatchesTheKmiManifestExactly() {
        val expected = labels().map { expectedName(it) }.toSortedSet()
        val actual = assetsDir.listFiles { file ->
            file.name.endsWith("-general.conf")
        }.orEmpty().map { it.name }.toSortedSet()
        assertEquals("general profiles must match lkm-kmi-manifest.tsv", expected, actual)
        for (name in actual) {
            val text = File(assetsDir, name).readText()
            val release = text.lines().first { it.trim().startsWith("release =") }.trim()
            assertEquals(name + ": release must be the general reserved name",
                "release = \"" + name.removeSuffix(".conf") + "\"", release)
        }
    }

    @Test
    fun everyGeneralProfileDefaultsToThe43284Queue() {
        val generals = assetsDir.listFiles { file ->
            file.name.endsWith("-general.conf")
        }.orEmpty()
        assertEquals("eight general profiles", 8, generals.size)
        for (file in generals) {
            val active = activeLines(availableBlock(file.readText()))
            val route = active.filter { it.contains("cve_2026_43284 {") }
            assertEquals(file.name + ": exactly one active 43284 declaration", 1, route.size)
            assertTrue(file.name + ": the 43284 declaration must carry a queue", route.single().contains("queue = ["))
            assertTrue(file.name + ": the queue must not be empty", !route.single().contains("queue = [ ]"))
            assertEquals(file.name + ": 43499 must stay commented inside available",
                0, active.count { it.contains("cve_2026_43499 {") })
        }
    }

    @Test
    fun theOldGeneralNamingIsGone() {
        /* Both the file names and the reserved index release names moved to
         * <family>-android<NN>-general; a leftover old form means two vocabularies. */
        val oldFiles = assetsDir.listFiles { f -> f.name.startsWith("general-") }.orEmpty()
        assertEquals("old general-<label>.conf files must not exist", 0, oldFiles.size)
        val index = File(assetsDir, "index.conf").readText()
        val oldReleases = index.lines().filter { it.contains("release = \"general-") }
        assertEquals("old reserved release names must not exist", 0, oldReleases.size)
    }

    @Test
    fun everyGeneralProfileInnerReleaseMatchesTheIndexEntry() {
        /* ProfileExporter enforces index release == document release; the batch 2 rename
         * broke exactly that invariant once, so it is pinned here: every general document
         * must carry the manifest-derived identity and the index must agree with it. */
        val index = File(assetsDir, "index.conf").readText().lines()
        for (label in labels()) {
            val name = expectedName(label)
            val rel = name.removeSuffix(".conf")
            val file = File(assetsDir, name)
            assertTrue(name + " must exist", file.isFile)
            val inner = file.readText().lines().first { it.trim().startsWith("release =") }.trim()
            assertEquals(name + ": inner release must be the manifest-derived identity", "release = " + '"' + rel + '"', inner)
            assertTrue(name + ": index must register the same release and file",
                index.any { it.contains(rel) && it.contains(name) })
        }
    }
}
