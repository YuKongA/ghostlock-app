package com.ghostlock.app.data

import com.ghostlock.app.data.component.BackendKind
import com.ghostlock.app.data.component.CombinationCatalog
import com.ghostlock.app.data.component.CombinationSpec
import com.ghostlock.app.data.component.ComponentAvailability
import com.ghostlock.app.data.component.FrontendKind
import com.ghostlock.app.data.route.RouteKind
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * S4 R6b / ADR-0006 T5: the app-side combination-selection behaviour. The
 * catalogue content itself now comes from the native-exported manifest (F4,
 * see CombinationTokenAgreementTest); this test pins the selection semantics the
 * dropdown and the profile builder rely on.
 */
class CombinationSelectionTest {

    private fun combination(token: String): CombinationSpec =
        requireNotNull(CombinationCatalog.resolve(token)) { token }

    @Test
    fun catalogHasExactlyThe12NativeTokensInNativeOrder() {
        assertEquals(12, CombinationCatalog.specs.size)
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
    fun availableAndPlannedCountsAre7And5() {
        assertEquals(7, CombinationCatalog.specs.count { it.available })
        assertEquals(5, CombinationCatalog.specs.count { !it.available })
        assertEquals(
            listOf("mcast_umh", "pselect_umh", "tcp_umh", "rootchild", "shizuku"),
            CombinationCatalog.specs.filterNot { it.available }.map { it.token },
        )
    }

    @Test
    fun everyAvailableTokenMapsToAWiredBackendStepSetAndTerminal() {
        for (kind in CombinationCatalog.specs.filter { it.available }) {
            assertTrue("backend unavailable for " + kind.token, kind.backend.available)
            assertTrue("terminal unavailable for " + kind.token, kind.terminal.available)
            when (kind.steps) {
                StepSetKind.W1W2, StepSetKind.W1W3 -> {
                    assertEquals(
                        "w1_w2/w1_w3 must hand off to root_child (" + kind.token + ")",
                        FrontendKind.RootChild,
                        kind.terminal,
                    )
                }

                StepSetKind.PAGE_CACHE_WRITE -> {
                    assertEquals(
                        "pagecache_write must hand off to umh_forward (" + kind.token + ")",
                        FrontendKind.UmhForward,
                        kind.terminal,
                    )
                }
            }
            /* A route axis is owned by 43499 only; 43284 carries a bare token. */
            assertEquals(
                "route-axis mismatch for " + kind.token,
                kind.backend == BackendKind.Cve2026_43499,
                kind.route != null,
            )
        }
    }

    @Test
    fun plannedTokensAreUnavailable() {
        assertEquals(
            listOf("mcast_umh", "pselect_umh", "tcp_umh"),
            CombinationCatalog.forBackend(BackendKind.Cve2026_43499).filterNot { it.available }.map { it.token },
        )
        assertEquals(
            listOf("rootchild", "shizuku"),
            CombinationCatalog.forBackend(BackendKind.Cve2026_43284).filterNot { it.available }.map { it.token },
        )
        assertEquals(
            listOf(
                "mcast_rootchild", "pselect_rootchild", "tcp_rootchild",
                "mcast_shizuku", "pselect_shizuku", "tcp_shizuku",
            ),
            CombinationCatalog.availableForBackend(BackendKind.Cve2026_43499).map { it.token },
        )
        assertEquals(
            listOf("umh"),
            CombinationCatalog.availableForBackend(BackendKind.Cve2026_43284).map { it.token },
        )
    }

    @Test
    fun resolveIsExactAndPerBackend() {
        /* The same spelling belongs to exactly one backend. */
        assertNull(CombinationCatalog.resolve(BackendKind.Cve2026_43284, "mcast_rootchild"))
        assertNull(CombinationCatalog.resolve(BackendKind.Cve2026_43499, "rootchild"))
        assertEquals(
            combination("mcast_rootchild"),
            CombinationCatalog.resolve(BackendKind.Cve2026_43499, "mcast_rootchild"),
        )
        /* EXACT is the cross-language contract surface: native
         * combination_resolve compares bytes, so the leniency native does not
         * have (case, surrounding whitespace) must not resolve here either. */
        assertNull(CombinationCatalog.resolve(BackendKind.Cve2026_43284, "UMH"))
        assertNull(CombinationCatalog.resolve("  TCP_Shizuku "))
        assertNull(CombinationCatalog.resolve(BackendKind.Cve2026_43499, " MCAST_ROOTCHILD"))
        /* Unknown and null tokens fail closed. */
        assertNull(CombinationCatalog.resolve("not_a_token"))
        assertNull(CombinationCatalog.resolve(BackendKind.Cve2026_43499, null))
        assertNull(CombinationCatalog.resolve(null))
    }

    @Test
    fun normalizeIsTheHoconInputBoundaryOnly() {
        assertEquals("tcp_shizuku", CombinationCatalog.normalize("  TCP_Shizuku "))
        assertEquals("umh", CombinationCatalog.normalize("UMH"))
        assertEquals("", CombinationCatalog.normalize("   "))
        assertNull(CombinationCatalog.normalize(null))
        /* A normalized value must still resolve; only the canonical token may
         * reach the wire. */
        assertEquals(
            combination("umh"),
            CombinationCatalog.resolve(CombinationCatalog.normalize("  UMH ")),
        )
        assertNull(CombinationCatalog.resolve(CombinationCatalog.normalize("not_a_token")))
        assertNull(CombinationCatalog.resolve(CombinationCatalog.normalize("   ")))
    }

    @Test
    fun fromLegacyStepsMapsThroughTheRouteAndPagecache() {
        assertEquals(
            combination("mcast_shizuku"),
            CombinationCatalog.fromLegacySteps(BackendKind.Cve2026_43499, "w1_w2", RouteKind.MULTICAST_WAITER),
        )
        assertEquals(
            combination("tcp_shizuku"),
            CombinationCatalog.fromLegacySteps(BackendKind.Cve2026_43499, "W1_W2", RouteKind.TCP_ZEROCOPY),
        )
        assertEquals(
            combination("pselect_rootchild"),
            CombinationCatalog.fromLegacySteps(BackendKind.Cve2026_43499, "w1_w3", RouteKind.SELECT_STACK),
        )
        assertEquals(
            combination("tcp_rootchild"),
            CombinationCatalog.fromLegacySteps(BackendKind.Cve2026_43499, "w1_w3", RouteKind.TCP_ZEROCOPY),
        )
        assertEquals(
            combination("umh"),
            CombinationCatalog.fromLegacySteps(BackendKind.Cve2026_43284, "pagecache_write", null),
        )
        /* A route is mandatory for a 43499 step-set token. */
        assertNull(CombinationCatalog.fromLegacySteps(BackendKind.Cve2026_43499, "w1_w3", null))
        /* Unrelated backends and unknown steps fail closed. */
        assertNull(CombinationCatalog.fromLegacySteps(BackendKind.Cve2026_64560, "w1_w3", RouteKind.TCP_ZEROCOPY))
        assertNull(CombinationCatalog.fromLegacySteps(BackendKind.Cve2026_43499, "not_a_step", RouteKind.TCP_ZEROCOPY))
        /* An already-canonical token round-trips without a route. */
        assertEquals(
            combination("tcp_rootchild"),
            CombinationCatalog.fromLegacySteps(BackendKind.Cve2026_43499, "tcp_rootchild", null),
        )
    }

    @Test
    fun defaultIsTheFirstAvailableWiredToken() {
        assertEquals(combination("mcast_rootchild"), CombinationCatalog.defaultSpec)
        assertTrue(CombinationCatalog.defaultSpec.available)
        assertEquals(BackendKind.Cve2026_43499, CombinationCatalog.defaultSpec.backend)
    }

    @Test
    fun availabilityMirrorsTheComponentCatalog() {
        for (kind in CombinationCatalog.specs) {
            assertEquals(
                "terminal availability drifted for " + kind.token,
                ComponentAvailability.frontendAvailable(kind.terminal),
                kind.terminal.available,
            )
            assertFalse(kind.available && !kind.backend.available)
        }
    }
}
