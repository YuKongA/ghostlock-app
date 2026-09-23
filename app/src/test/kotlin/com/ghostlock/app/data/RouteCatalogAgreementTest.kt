package com.ghostlock.app.data

import com.ghostlock.app.data.route.RouteKind
import org.junit.Assert.assertEquals
import org.junit.Test

/**
 * Cross-language agreement anchor: the canonical (token, wire) set. The native
 * `route_catalog_test` asserts the same list; drift on either side fails its
 * own test.
 */
class RouteCatalogAgreementTest {
    @Test
    fun tokensAndWiresMatchNative() {
        val expected = listOf(
            "tcp_zerocopy" to 1u,
            "select_stack" to 2u,
            "multicast_waiter" to 3u,
        )
        assertEquals(expected, RouteKind.values().map { it.token to it.wire })
    }
}
