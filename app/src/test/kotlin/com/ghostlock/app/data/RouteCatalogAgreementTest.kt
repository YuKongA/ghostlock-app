package com.ghostlock.app.data

import com.ghostlock.app.data.component.VocabularyCatalog
import com.ghostlock.app.data.route.RouteKind
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * task-6 agreement for the route vocabulary: the (token, wire, available) set
 * comes from the native-exported vocabulary manifest, so no hand-written table
 * remains here. The native `kRouteCatalog` is still the authority (the manifest
 * is generated from it); this test fails if the Kotlin enum and the export
 * disagree in either direction OR in order.
 */
class RouteCatalogAgreementTest {
    @Test
    fun tokensAndWiresMatchTheNativeVocabularyManifest() {
        val rows = VocabularyCatalog.of("route")
        assertTrue("manifest declares no route row", rows.isNotEmpty())
        /* Order is part of the agreement: the manifest is the catalogue order. */
        assertEquals(
            "RouteKind order drifted from the vocabulary manifest",
            rows.map { it.token },
            RouteKind.entries.map { it.token },
        )
        for (row in rows) {
            val kind = requireNotNull(RouteKind.resolve(row.token)) {
                "manifest route is missing from RouteKind: " + row.token
            }
            assertEquals("wire id drifted for " + row.token, row.wire.toUInt(), kind.wire)
            assertEquals(
                "availability drifted for " + row.token,
                row.available,
                kind.available,
            )
            assertEquals(kind, RouteKind.resolve(kind.token))
            assertEquals(kind, RouteKind.fromWire(row.wire.toUInt()))
        }
        assertEquals(
            "RouteKind has an entry the manifest does not declare",
            rows.size,
            RouteKind.entries.size,
        )
    }
}
