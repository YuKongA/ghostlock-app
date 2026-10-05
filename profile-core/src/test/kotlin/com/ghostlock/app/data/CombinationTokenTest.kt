package com.ghostlock.app.data.component

import com.ghostlock.app.data.StepSetKind
import com.ghostlock.app.data.route.RouteKind
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * S4 R6b / ADR-0006 T5: the combination-token vocabulary parsed from the
 * native-exported combination manifest (F4). These assertions pin the token ->
 * (route, steps, terminal) derivation and the availability semantics. The
 * manifest content is asserted against native by combination_manifest_test and
 * the app test-resource copy is cross-checked by CombinationTokenAgreementTest.
 */
class CombinationTokenTest {

    private fun combination(token: String): CombinationSpec =
        requireNotNull(CombinationCatalog.resolve(token)) { token }

    @Test
    fun theCatalogContainsExactlyThe12NativeTokensInNativeOrder() {
        assertEquals(
            listOf(
                "mcast_rootchild", "pselect_rootchild", "tcp_rootchild",
                "mcast_shizuku", "pselect_shizuku", "tcp_shizuku",
                "mcast_umh", "pselect_umh", "tcp_umh",
                "umh", "rootchild", "shizuku",
            ),
            CombinationCatalog.specs.map { it.token },
        )
    }

    @Test
    fun onlyPlannedTokensAreUnavailable() {
        val planned = CombinationCatalog.specs.filterNot { it.available }.map { it.token }
        assertEquals(
            listOf("mcast_umh", "pselect_umh", "tcp_umh", "rootchild", "shizuku"),
            planned,
        )
        assertEquals(7, CombinationCatalog.specs.count { it.available })
        assertEquals(5, planned.size)
    }

    @Test
    fun tokenDerivesRouteStepSetAndTerminal() {
        val mcast = combination("mcast_rootchild")
        assertEquals(BackendKind.Cve2026_43499, mcast.backend)
        assertEquals(RouteKind.MULTICAST_WAITER, mcast.route)
        assertEquals(StepSetKind.W1W3, mcast.steps)
        assertEquals(FrontendKind.RootChild, mcast.terminal)
        assertEquals("rootchild", mcast.path)
        assertTrue(mcast.hasRouteAxis)

        val shizuku = combination("tcp_shizuku")
        assertEquals(RouteKind.TCP_ZEROCOPY, shizuku.route)
        assertEquals(StepSetKind.W1W2, shizuku.steps)

        val umh = combination("umh")
        assertEquals(BackendKind.Cve2026_43284, umh.backend)
        assertNull(umh.route)
        assertEquals("umh", umh.path)
        assertEquals(StepSetKind.PAGE_CACHE_WRITE, umh.steps)
        assertEquals(FrontendKind.UmhForward, umh.terminal)
        assertFalse(umh.hasRouteAxis)
    }

    @Test
    fun docCarriesTheDropdownSummary() {
        assertEquals(
            "cve_2026_43499 · multicast_waiter · w1_w3 · root_child",
            combination("mcast_rootchild").doc,
        )
        assertEquals(
            "cve_2026_43284 · pagecache_write · umh_forward",
            combination("umh").doc,
        )
    }

    @Test
    fun resolveIsExactAndPerBackend() {
        /* rootchild is a 43284 token; 43499 must not resolve it. */
        assertNull(CombinationCatalog.resolve(BackendKind.Cve2026_43499, "rootchild"))
        assertEquals(
            combination("mcast_rootchild"),
            CombinationCatalog.resolve(BackendKind.Cve2026_43499, "mcast_rootchild"),
        )
        /* EXACT match mirrors native combination_resolve: case and whitespace
         * are NOT included leniency (that lives in normalize, at the HOCON/UI
         * boundary only). */
        assertNull(CombinationCatalog.resolve(BackendKind.Cve2026_43284, "UMH"))
        assertNull(CombinationCatalog.resolve("  mcast_rootchild "))
        assertNull(CombinationCatalog.resolve("not_a_token"))
        assertNull(CombinationCatalog.resolve(BackendKind.Cve2026_43499, null))
        /* The boundary helper is what turns a user spelling into a token. */
        assertEquals("umh", CombinationCatalog.normalize("  UMH "))
        assertEquals(
            combination("umh"),
            CombinationCatalog.resolve(CombinationCatalog.normalize("  UMH ")),
        )
    }

    @Test
    fun legacyStepIdMigratesThroughTheDeclaredRoute() {
        assertEquals(
            combination("mcast_shizuku"),
            CombinationCatalog.fromLegacySteps(BackendKind.Cve2026_43499, "w1_w2", RouteKind.MULTICAST_WAITER),
        )
        assertEquals(
            combination("pselect_rootchild"),
            CombinationCatalog.fromLegacySteps(BackendKind.Cve2026_43499, "w1_w3", RouteKind.SELECT_STACK),
        )
        /* A backend with no catalogue entry fails closed. */
        assertNull(CombinationCatalog.fromLegacySteps(BackendKind.Cve2026_64560, "w1_w3", RouteKind.TCP_ZEROCOPY))
        assertNull(CombinationCatalog.fromLegacySteps(BackendKind.Cve2026_43499, "w1_w3", null))
        /* An already-canonical token round-trips without a route. */
        assertEquals(
            combination("tcp_rootchild"),
            CombinationCatalog.fromLegacySteps(BackendKind.Cve2026_43499, "tcp_rootchild", null),
        )
        assertEquals(
            combination("umh"),
            CombinationCatalog.fromLegacySteps(BackendKind.Cve2026_43284, "pagecache_write", null),
        )
    }

    @Test
    fun plannedTokensParseButAreNotAvailable() {
        val planned = CombinationCatalog.resolve(BackendKind.Cve2026_43284, "rootchild")
        assertEquals(combination("rootchild"), planned)
        assertFalse(planned!!.available)
        assertEquals(3, CombinationCatalog.forBackend(BackendKind.Cve2026_43284).size)
        assertEquals(
            listOf(combination("umh")),
            CombinationCatalog.availableForBackend(BackendKind.Cve2026_43284),
        )
    }

    @Test
    fun defaultForAndRecommendedPickTheFirstAvailableRow() {
        /* defaultFor = the first SELECTABLE row of the backend (the dev entry
         * uses it to select the 43284 path without naming a token literal). */
        assertEquals(combination("mcast_rootchild"), CombinationCatalog.defaultFor(BackendKind.Cve2026_43499))
        assertEquals(combination("umh"), CombinationCatalog.defaultFor(BackendKind.Cve2026_43284))
        /* recommended = first available row whose derived route matches, and
         * the first available row when no route matches (or none is pinned). */
        assertEquals(
            combination("pselect_rootchild"),
            CombinationCatalog.recommended(BackendKind.Cve2026_43499, RouteKind.SELECT_STACK),
        )
        /* The first available row for the route wins: rootchild precedes the
         * shizuku row because catalogue order is the selection order. */
        assertEquals(
            combination("tcp_rootchild"),
            CombinationCatalog.recommended(BackendKind.Cve2026_43499, RouteKind.TCP_ZEROCOPY),
        )
        assertEquals(
            combination("mcast_rootchild"),
            CombinationCatalog.recommended(BackendKind.Cve2026_43499, null),
        )
        assertEquals(
            combination("umh"),
            CombinationCatalog.recommended(BackendKind.Cve2026_43284, RouteKind.SELECT_STACK),
        )
    }
}
