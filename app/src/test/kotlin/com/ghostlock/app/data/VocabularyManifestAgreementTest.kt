package com.ghostlock.app.data

import com.ghostlock.app.data.component.VocabularyCatalog
import java.io.File
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * task-6 agreement: the native vocabulary export is committed twice, and the
 * App's runtime catalog parses it.
 *
 * The copies are compared through their REPOSITORY paths, not through the test
 * classpath (where only one of them can win): the comparison is deliberately
 * non-self-referential, so it holds independently of classpath ordering. The
 * structural assertions are derived from the resource, never from a literal
 * table.
 */
class VocabularyManifestAgreementTest {

    private fun repoRoot(): File {
        var current = File(requireNotNull(System.getProperty("user.dir"))).canonicalFile
        repeat(5) {
            if (File(current, "app/src/test/resources/vocabulary-manifest.tsv").isFile) {
                return current
            }
            current = current.parentFile ?: error("cannot locate repository root")
        }
        error("cannot locate repository root")
    }

    private fun appCopy(): File = File(repoRoot(), "app/src/test/resources/vocabulary-manifest.tsv")

    private fun runtimeCopy(): File =
        File(repoRoot(), "profile-core/src/main/resources/vocabulary-manifest.tsv")

    @Test
    fun `the two exported copies are byte identical`() {
        assertTrue("missing app fixture: " + appCopy().path, appCopy().isFile)
        assertTrue("missing runtime resource: " + runtimeCopy().path, runtimeCopy().isFile)
        assertArrayEquals(
            "vocabulary-manifest.tsv drifted between the app test resource and the " +
                "profile-core runtime resource (regenerate with make -C src vocabulary-manifest)",
            runtimeCopy().readBytes(),
            appCopy().readBytes(),
        )
    }

    @Test
    fun `both copies parse to the same rows as the runtime catalog`() {
        val appRows = VocabularyCatalog.parse(appCopy().readText())
        val runtimeRows = VocabularyCatalog.parse(runtimeCopy().readText())
        assertEquals(appRows, runtimeRows)
        assertEquals(
            "the runtime resource the App loads drifted from the committed fixture",
            appRows,
            VocabularyCatalog.entries,
        )
    }

    @Test
    fun `every kind is internally unique and has an available row`() {
        val kinds = VocabularyCatalog.entries.map { it.kind }.distinct()
        assertTrue("manifest declares no kind", kinds.isNotEmpty())
        for (kind in kinds) {
            val rows = VocabularyCatalog.of(kind)
            assertEquals(
                "duplicate token in kind " + kind,
                rows.size,
                rows.map { it.token }.distinct().size,
            )
            assertEquals(
                "duplicate wire id in kind " + kind,
                rows.size,
                rows.map { it.wire }.distinct().size,
            )
            assertTrue(
                "kind " + kind + " declares no available row",
                rows.any { it.available },
            )
            assertTrue(
                "kind " + kind + " has an empty doc",
                rows.all { it.doc.isNotBlank() },
            )
        }
    }

    @Test
    fun `malformed manifests fail closed with the offending line`() {
        val header = "# test\n"
        val valid = "backend\tcve_2026_43499\t1\t1\tdoc\n"
        assertEquals(1, VocabularyCatalog.parse(header + valid).size)
        fun rejects(line: String) {
            val error = org.junit.Assert.assertThrows(IllegalArgumentException::class.java) {
                VocabularyCatalog.parse(header + valid + line)
            }
            assertTrue(error.message!!.isNotBlank())
        }
        rejects("backend\tcve_2026_43499\t7\t1\tdoc\n")
        rejects("backend\tcve_2026_99999\t1\t1\tdoc\n")
        rejects("backend\tbad\tnotanumber\t1\tdoc\n")
        rejects("backend\tbad\t9\t2\tdoc\n")
        rejects("backend\t\t9\t1\tdoc\n")
        rejects("BAD KIND\tbad\t9\t1\tdoc\n")
        rejects("backend\tbad\t9\t1\n")
        org.junit.Assert.assertThrows(IllegalArgumentException::class.java) {
            VocabularyCatalog.parse(header)
        }
    }
}
