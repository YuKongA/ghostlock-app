package com.ghostlock.app.data.plugin

import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Three-end probe TSV conformance corpus (S4 plugin audit): the SAME fixtures
 * are walked by native, by the Rust extractor and here.
 *
 * Agreement rules (design §4), applied without any per-case expectation:
 *  1. the manifest is fail-closed — `verdict<TAB>basename`, two columns, a legal
 *     verdict, unique names;
 *  2. completeness in BOTH directions — the enumerated files and the manifest
 *     entries must be identical (an unregistered file fails, a registered but
 *     missing file fails), plus a scale floor so the corpus cannot shrink
 *     silently;
 *  3. every accept case parses, and its INVARIANTS are recomputed from the raw
 *     bytes (plugin id, param/extract row counts, per-row name/type/required,
 *     the plugin row's stages and required_caps columns);
 *  4. every reject case fails to parse. The error TEXT is not asserted: the
 *     three ends legitimately word it differently.
 */
class PluginProbeConformanceTest {

    private fun repoRoot(): File {
        var current = File(requireNotNull(System.getProperty("user.dir"))).canonicalFile
        repeat(5) {
            if (File(current, "app/src/test/resources/plugin-probe-conformance").isDirectory) {
                return current
            }
            current = current.parentFile ?: error("cannot locate the repository root")
        }
        error("cannot locate the repository root")
    }

    private fun corpusDir(): File =
        File(repoRoot(), "app/src/test/resources/plugin-probe-conformance")

    private data class Case(val verdict: String, val name: String, val file: File)

    private fun manifestCases(): List<Case> {
        val manifest = File(corpusDir(), "manifest.tsv")
        assertTrue("missing corpus manifest: " + manifest.path, manifest.isFile)
        val cases = mutableListOf<Case>()
        val seen = mutableSetOf<String>()
        for (raw in manifest.readText().lineSequence()) {
            val line = raw.trimEnd('\r')
            if (line.isBlank() || line.startsWith("#")) continue
            val parts = line.split('\t')
            assertEquals("manifest line needs two columns: " + line, 2, parts.size)
            val verdict = parts[0]
            val name = parts[1]
            assertTrue("illegal verdict in the manifest: " + line, verdict in setOf("accept", "reject"))
            assertTrue("empty case name in the manifest: " + line, name.isNotBlank())
            assertTrue("duplicate case in the manifest: " + name, seen.add(name))
            cases += Case(verdict, name, File(corpusDir(), verdict + "/" + name))
        }
        assertTrue("the corpus manifest declares no case", cases.isNotEmpty())
        return cases
    }

    /** filenames of one directory, or an empty set when it does not exist. */
    private fun filesIn(verdict: String): Set<String> =
        File(corpusDir(), verdict).listFiles()
            ?.filter { it.isFile }
            ?.map { it.name }
            ?.toSet()
            ?: emptySet()

    @Test
    fun `the manifest and the two directories agree in both directions`() {
        val cases = manifestCases()
        val accept = cases.filter { it.verdict == "accept" }
        val reject = cases.filter { it.verdict == "reject" }
        assertEquals("registered accept cases vs accept/ files", accept.map { it.name }.toSet(), filesIn("accept"))
        assertEquals("registered reject cases vs reject/ files", reject.map { it.name }.toSet(), filesIn("reject"))
        for (case in cases) {
            assertTrue("the manifest registers a missing file: " + case.file.path, case.file.isFile)
        }
        /* Anti-shrink floor: the corpus is evidence, not a sample. */
        assertTrue("the corpus shrank below 60 cases", cases.size >= 60)
        assertTrue("the corpus has no reject case", reject.isNotEmpty())
        /* One readable line for the gate record. */
        println("probe conformance: " + accept.size + " accept / " + reject.size + " reject cases")
    }

    @Test
    fun `every accept case parses and keeps its raw invariants`() {
        val cases = manifestCases().filter { it.verdict == "accept" }
        assertTrue(cases.isNotEmpty())
        /* Collect instead of failing fast, so one run names EVERY disagreement. */
        val failures = mutableListOf<String>()
        for (case in cases) {
            val text = case.file.readBytes().toString(Charsets.UTF_8)
            val descriptor = try {
                PluginProbe.parse(text)
            } catch (error: Exception) {
                failures += "accept case " + case.name + " did not parse: " + error.message
                continue
            }
            val lines = text.lines().map { it.trimEnd('\r') }.filter { it.isNotBlank() }
            fun rows(kind: String) = lines.filter { it.substringBefore('\t') == kind }
            val pluginRows = rows("plugin")
            assertEquals("case " + case.name + " needs exactly one plugin row", 1, pluginRows.size)
            val pluginCols = pluginRows.single().split('\t')
            assertEquals("plugin id, case " + case.name, pluginCols[1], descriptor.id)

            fun column(col: String) =
                col.split(',').map { it.trim() }.filter { it.isNotEmpty() && it != "-" }.toSet()
            assertEquals("stages, case " + case.name, column(pluginCols[6]), descriptor.stages)
            assertEquals(
                "required caps, case " + case.name,
                column(pluginCols[7]),
                descriptor.requiredCaps,
            )

            val paramRows = rows("param")
            val extractRows = rows("extract")
            assertEquals("param rows, case " + case.name, paramRows.size, descriptor.params.size)
            assertEquals("extract rows, case " + case.name, extractRows.size, descriptor.extract.size)
            assertRowsMatch(case.name, paramRows, descriptor.params)
            assertRowsMatch(case.name, extractRows, descriptor.extract)
            assertEquals("hook rows, case " + case.name, rows("hook").size, descriptor.hooks.size)
            assertEquals("reject rows, case " + case.name, rows("reject").size, descriptor.rejects.size)
        }
        assertTrue("accept cases disagreed: " + failures.joinToString(" | "), failures.isEmpty())
    }

    private fun assertRowsMatch(
        caseName: String,
        rawRows: List<String>,
        parsed: List<PluginParam>,
    ) {
        assertEquals("rows and parsed entries must line up, case " + caseName, rawRows.size, parsed.size)
        rawRows.zip(parsed).forEachIndexed { index, (row, param) ->
            val cols = row.split('\t')
            val where = "case " + caseName + " row " + index
            assertEquals("name, " + where, cols[2], param.name)
            assertEquals("type, " + where, cols[3], param.type.text)
            assertEquals("required, " + where, cols[4] == "1", param.required)
        }
    }

    @Test
    fun `every reject case is refused`() {
        val cases = manifestCases().filter { it.verdict == "reject" }
        assertTrue(cases.isNotEmpty())
        for (case in cases) {
            val text = case.file.readBytes().toString(Charsets.UTF_8)
            val parsed = runCatching { PluginProbe.parse(text) }
            assertTrue(
                "reject case " + case.name + " must NOT parse",
                parsed.isFailure,
            )
        }
    }
}
