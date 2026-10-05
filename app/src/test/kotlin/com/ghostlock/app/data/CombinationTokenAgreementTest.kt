package com.ghostlock.app.data

import com.ghostlock.app.data.component.BackendKind
import com.ghostlock.app.data.component.CombinationCatalog
import com.ghostlock.app.ui.combinationOptions
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File

/**
 * F4 cross-language agreement anchor for the combination-token manifest.
 *
 * `contract::kCombinationCatalog` (native) is the single authority; it is
 * exported by `combination_manifest_test` (see
 * `make -C src combination-manifest`) into:
 *
 *  - `app/src/test/resources/combination-manifest.tsv` (this test's fixture);
 *  - `profile-core/src/main/resources/combination-manifest.tsv` (the runtime
 *    resource `CombinationCatalog` parses);
 *  - `app/src/test/resources/combination-resolve-vectors.tsv`, the native
 *    resolution vectors (backend / input / expected token, with \s, \t and
 *    \\ escapes), single copy on purpose.
 *
 * This test fails if the two manifest copies drift, if the runtime catalogue
 * disagrees with a row's derived columns, if the dropdown presentation
 * (`combinationOptions`/`CombinationSpec.doc`) offers anything the manifest does
 * not declare, or if `resolve` gives a different verdict than native
 * `contract::combination_resolve` for any exported vector.
 *
 * The two manifest copies are compared through their REPOSITORY paths, not
 * through the test classpath (where only one of them can win): the comparison is
 * deliberately not self-referential, so it holds independently of classpath
 * ordering.
 */
class CombinationTokenAgreementTest {

    private data class Row(
        val token: String,
        val backend: String,
        val route: String,
        val path: String,
        val steps: String,
        val terminal: String,
        val available: Boolean,
        val doc: String,
    )

    private fun repoRoot(): File {
        var current = File(requireNotNull(System.getProperty("user.dir"))).canonicalFile
        repeat(5) {
            if (File(current, "app/src/test/resources/combination-manifest.tsv").isFile) {
                return current
            }
            current = current.parentFile ?: error("cannot locate repository root")
        }
        error("cannot locate repository root")
    }

    private fun appFixture(): File =
        File(repoRoot(), "app/src/test/resources/combination-manifest.tsv")

    private fun runtimeResource(): File =
        File(repoRoot(), "profile-core/src/main/resources/combination-manifest.tsv")

    private fun classpathFixture(name: String): String = checkNotNull(
        javaClass.classLoader?.getResourceAsStream(name),
    ) { "missing native-exported fixture: " + name }.bufferedReader().use { it.readText() }

    /** Independent parse of the committed rows (a test-side second reader). */
    private fun parse(text: String): List<Row> = text.lineSequence()
        .filter { it.isNotBlank() && !it.startsWith("#") }
        .map { raw ->
            val line = raw.trimEnd('\r')
            val parts = line.split('\t')
            assertEquals("manifest line needs 8 tab-separated columns: $line", 8, parts.size)
            Row(
                token = parts[0],
                backend = parts[1],
                route = parts[2],
                path = parts[3],
                steps = parts[4],
                terminal = parts[5],
                available = parts[6] == "1",
                doc = parts[7],
            )
        }
        .toList()

    /** Expands the vector column escapes documented by the native exporter. */
    private fun unescape(input: String): String {
        val out = StringBuilder()
        var index = 0
        while (index < input.length) {
            val char = input[index]
            if (char == '\\' && index + 1 < input.length) {
                when (input[index + 1]) {
                    's' -> { out.append(' '); index += 2; continue }
                    't' -> { out.append('\t'); index += 2; continue }
                    '\\' -> { out.append('\\'); index += 2; continue }
                }
            }
            out.append(char)
            index += 1
        }
        return out.toString()
    }

    @Test
    fun theTwoExportedCopiesAreByteIdentical() {
        assertTrue("missing app fixture", appFixture().isFile)
        assertTrue("missing runtime resource", runtimeResource().isFile)
        assertArrayEquals(
            "app test resource and profile-core runtime resource drifted",
            runtimeResource().readBytes(),
            appFixture().readBytes(),
        )
    }

    @Test
    fun runtimeCatalogMatchesTheExportedRows() {
        val rows = parse(appFixture().readText())
        assertTrue("manifest fixture is empty", rows.isNotEmpty())
        assertEquals(
            "catalogue order must match the manifest",
            rows.map { it.token },
            CombinationCatalog.specs.map { it.token },
        )
        for ((row, spec) in rows.zip(CombinationCatalog.specs)) {
            assertEquals(row.token, spec.token)
            assertEquals(row.backend, spec.backend.token)
            assertEquals("route presence drifted for " + row.token, row.route == "none", spec.route == null)
            assertEquals(row.route, spec.route?.token ?: "none")
            assertEquals(row.path, spec.path)
            assertEquals(row.steps, spec.steps.token)
            assertEquals(row.terminal, spec.terminal.token)
            assertEquals(row.available, spec.available)
            assertEquals(row.doc, spec.doc)
            assertEquals(row.route != "none", spec.hasRouteAxis)
            /* Structural token/route/path agreement. The route PREFIX is an
             * abbreviation owned by the native table, so only the shape is
             * asserted here: bare <path> without a route axis, <prefix>_<path>
             * with one; the prefix -> route pairing is asserted row by row
             * above (row.route vs spec.route). */
            if (row.route == "none") {
                assertEquals("token without a route axis must be the bare path", row.path, row.token)
            } else {
                assertTrue(
                    "token with a route axis must be <prefix>_<path>: " + row.token,
                    row.token.length > row.path.length + 1 &&
                        row.token.endsWith("_" + row.path),
                )
            }
        }
    }

    @Test
    fun dropdownPresentationMatchesTheExportedRows() {
        val rows = parse(appFixture().readText())
        val options = combinationOptions()
        assertEquals(rows.map { it.token }, options.map { it.spec.token })
        assertEquals(rows.map { it.available }, options.map { it.enabled })
        assertEquals(rows.map { !it.available }, options.map { it.planned })
        assertEquals(rows.map { it.doc }, options.map { it.spec.doc })
        assertTrue(options.isNotEmpty())
    }

    /**
     * Every native resolution vector must get the SAME verdict here. The
     * vectors cover the exact-match contract (canonical tokens, cross-backend
     * rejection, case/wildcard/empty/unknown inputs and both space escapes), so
     * a difference in trimming, case folding or backend scoping fails here
     * instead of reaching the wire.
     */
    @Test
    fun resolveAgreesWithTheNativeResolveVectors() {
        val text = classpathFixture("combination-resolve-vectors.tsv")
        var checked = 0
        for (raw in text.lineSequence()) {
            val line = raw.trimEnd('\r')
            if (line.isBlank() || line.startsWith("#")) continue
            val parts = line.split('\t')
            assertEquals("resolve-vector line needs 3 tab-separated columns: $line", 3, parts.size)
            val backend = requireNotNull(BackendKind.resolve(parts[0])) {
                "unknown backend in resolve vectors: " + line
            }
            val input = unescape(parts[1])
            val expected = parts[2].takeIf { it != "--" }
            val actual = CombinationCatalog.resolve(backend, input)?.token
            assertEquals(
                "resolve disagrees with the native vector (backend=" + parts[0] +
                    ", input=" + parts[1] + "): " + line,
                expected,
                actual,
            )
            checked++
        }
        assertTrue("resolve-vector fixture is empty", checked > 0)
    }

    /**
     * The resolution contract is EXACT, mirroring native
     * `contract::combination_resolve` (src/core/contract/identity.hpp): every
     * exported token resolves in its own backend and nowhere else, and no
     * spelling the native byte comparison would reject is accepted here.
     */
    @Test
    fun resolveIsExactForEveryExportedRow() {
        val rows = parse(appFixture().readText())
        for (row in rows) {
            val backend = requireNotNull(BackendKind.resolve(row.backend)) { row.backend }
            val spec = CombinationCatalog.resolve(backend, row.token)
            assertEquals("exact token must resolve in its own backend", row.token, spec?.token)
        }
        val foreign = mapOf(
            "cve_2026_43499" to "rootchild",
            "cve_2026_43284" to "mcast_rootchild",
        )
        for ((backendToken, token) in foreign) {
            val backend = requireNotNull(BackendKind.resolve(backendToken)) { backendToken }
            assertNull("cross-backend token must not resolve: " + token, CombinationCatalog.resolve(backend, token))
        }
        /* Non-canonical spellings are input-boundary material, not tokens. */
        assertNull(CombinationCatalog.resolve("  mcast_rootchild "))
        assertNull(CombinationCatalog.resolve("MCAST_ROOTCHILD"))
        assertNull(CombinationCatalog.resolve("not_a_token"))
        assertNull(CombinationCatalog.resolve(null))
        assertEquals("mcast_rootchild", CombinationCatalog.normalize("  MCAST_ROOTCHILD "))
    }
}
