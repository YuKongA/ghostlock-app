package com.ghostlock.app.data.component

import com.ghostlock.app.data.StepSetKind
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * task-6: the App-side component model is pinned to the native-exported
 * vocabulary manifest.
 *
 * Every expectation here is DERIVED from the resource: the test walks the
 * manifest rows of each kind and asserts the matching enum entry, so a
 * vocabulary added, retyped or flipped to unavailable natively fails here
 * instead of drifting silently. No hand-written (token, wire, available) table
 * remains.
 */
class ComponentKindTest {

    @Test
    fun `backend vocabulary matches the native manifest`() {
        val rows = VocabularyCatalog.of("backend")
        assertTrue("manifest declares no backend row", rows.isNotEmpty())
        for (row in rows) {
            val kind = requireNotNull(BackendKind.resolve(row.token)) {
                "manifest backend is missing from BackendKind: " + row.token
            }
            assertEquals("wire id drifted for " + row.token, row.wire, kind.wire)
            assertEquals("availability drifted for " + row.token, row.available, kind.available)
            assertEquals(
                "availability predicate disagrees for " + row.token,
                row.available,
                ComponentAvailability.backendAvailable(kind),
            )
            assertEquals(kind, BackendKind.resolve(kind.token))
            assertEquals(kind, BackendKind.fromWire(row.wire))
        }
        assertEquals(
            "BackendKind has an entry the manifest does not declare",
            rows.size,
            BackendKind.entries.size,
        )
    }

    @Test
    fun `frontend vocabulary matches the native manifest`() {
        val rows = VocabularyCatalog.of("frontend")
        assertTrue("manifest declares no frontend row", rows.isNotEmpty())
        for (row in rows) {
            val kind = requireNotNull(FrontendKind.resolve(row.token)) {
                "manifest frontend is missing from FrontendKind: " + row.token
            }
            assertEquals(row.wire, kind.wire)
            assertEquals(row.available, kind.available)
            assertEquals(
                row.available,
                ComponentAvailability.frontendAvailable(kind),
            )
            assertEquals(kind, FrontendKind.fromWire(row.wire))
        }
        assertEquals(rows.size, FrontendKind.entries.size)
    }

    @Test
    fun `step set vocabulary matches the native manifest`() {
        val rows = VocabularyCatalog.of("stepset")
        assertTrue("manifest declares no stepset row", rows.isNotEmpty())
        for (row in rows) {
            val kind = requireNotNull(StepSetKind.resolve(row.token)) {
                "manifest step set is missing from StepSetKind: " + row.token
            }
            assertEquals(row.wire.toUInt(), kind.wire)
            assertEquals(row.available, kind.available)
            assertEquals(kind, StepSetKind.fromWire(row.wire.toUInt()))
        }
        assertEquals(rows.size, StepSetKind.entries.size)
        /* The native Unknown=0 sentinel is not a selectable vocabulary entry. */
        assertNull(StepSetKind.fromWire(0u))
    }

    @Test
    fun `catalog lookups are exact and per kind`() {
        val backend = VocabularyCatalog.of("backend").first()
        assertNotNull(VocabularyCatalog.resolve("backend", backend.token))
        assertEquals(backend, VocabularyCatalog.resolveWire("backend", backend.wire))
        /* A token of one kind never resolves inside another. */
        assertNull(VocabularyCatalog.resolve("frontend", backend.token))
        assertNull(VocabularyCatalog.resolve("backend", backend.token.uppercase()))
        assertNull(VocabularyCatalog.resolve("backend", null))
        assertNull(VocabularyCatalog.resolve("nope", backend.token))
        assertNull(VocabularyCatalog.resolveWire("backend", Int.MAX_VALUE))
    }

    @Test
    fun `placeholder backends are displayable but never selected`() {
        val placeholders = BackendKind.entries.filterNot { it.available }
        assertTrue("the manifest should declare placeholders", placeholders.isNotEmpty())
        for (kind in placeholders) {
            assertFalse(ComponentAvailability.backendAvailable(kind))
            assertEquals(
                "an unavailable backend must fall back to the default",
                BackendKind.Default,
                BackendKind.selectableOrFallback(kind),
            )
        }
        assertEquals(BackendKind.Cve2026_43499, BackendKind.selectableOrFallback(null))
    }

    @Test
    fun `available backend is selected as-is`() {
        for (kind in BackendKind.entries.filter { it.available }) {
            assertEquals(kind, BackendKind.selectableOrFallback(kind))
        }
    }

    @Test
    fun `stored backend spellings are migrated`() {
        assertEquals(BackendKind.Cve2026_43499, BackendKind.fromStored("cve_2026_43499"))
        assertEquals(BackendKind.Cve2026_43499, BackendKind.fromStored("Cve2026_43499"))
        assertEquals(BackendKind.Cve2026_43499, BackendKind.fromStored("1"))
        assertEquals(BackendKind.Cve2026_43284, BackendKind.fromStored("6"))
        assertEquals(BackendKind.Cve2026_43284, BackendKind.fromStored("cve_2026_43284"))
        assertNull(BackendKind.fromStored("bogus"))
        assertNull(BackendKind.fromStored(""))
        assertNull(BackendKind.fromStored(null))
    }
}
