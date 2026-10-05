package com.ghostlock.app.data

import com.ghostlock.app.data.component.BackendKind
import com.ghostlock.app.data.component.FrontendKind
import com.ghostlock.app.data.route.RouteKind
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

/**
 * task-7: the shared vocabulary contract.
 *
 * `resolve` is an EXACT lookup — the contract surface shared with native
 * (wire tokens, manifest rows, persisted canonical values, already-normalized
 * input). `normalize` is the trim + lower-case leniency that exists ONLY at a
 * HOCON/UI input boundary, whose result is then resolved:
 * `resolve(normalize(value))`. One spelling must never be accepted by one
 * vocabulary and rejected by another.
 */
class VocabularyResolveTest {

    @Test
    fun `route resolve is exact and normalize is the boundary`() {
        assertEquals(RouteKind.MULTICAST_WAITER, RouteKind.resolve("multicast_waiter"))
        assertNull(RouteKind.resolve("MULTICAST_WAITER"))
        assertNull(RouteKind.resolve(" multicast_waiter "))
        assertNull(RouteKind.resolve(""))
        assertNull(RouteKind.resolve(null))
        assertEquals("multicast_waiter", RouteKind.normalize("  Multicast_Waiter "))
        assertEquals("", RouteKind.normalize("   "))
        assertNull(RouteKind.normalize(null))
        assertEquals(
            RouteKind.SELECT_STACK,
            RouteKind.resolve(RouteKind.normalize("  SELECT_STACK ")),
        )
    }

    @Test
    fun `backend resolve is exact and normalize is the boundary`() {
        assertEquals(BackendKind.Cve2026_43499, BackendKind.resolve("cve_2026_43499"))
        assertNull(BackendKind.resolve("CVE_2026_43499"))
        assertNull(BackendKind.resolve(" cve_2026_43499 "))
        assertNull(BackendKind.resolve(""))
        assertNull(BackendKind.resolve(null))
        assertEquals("cve_2026_43284", BackendKind.normalize("  Cve_2026_43284 "))
        assertEquals(
            BackendKind.Cve2026_43284,
            BackendKind.resolve(BackendKind.normalize(" CVE_2026_43284")),
        )
        /* The persisted-preference parser keeps its historical spellings, but
         * normalizes once and then resolves exactly. */
        assertEquals(BackendKind.Cve2026_43499, BackendKind.fromStored("Cve2026_43499"))
        assertEquals(BackendKind.Cve2026_43284, BackendKind.fromStored("6"))
        assertNull(BackendKind.fromStored(" 	 "))
    }

    @Test
    fun `frontend resolve is exact and normalize is the boundary`() {
        assertEquals(FrontendKind.RootChild, FrontendKind.resolve("root_child"))
        assertEquals(FrontendKind.UmhForward, FrontendKind.resolve("umh_forward"))
        assertNull(FrontendKind.resolve("ROOT_CHILD"))
        assertNull(FrontendKind.resolve(" root_child "))
        assertNull(FrontendKind.resolve(null))
        assertEquals("umh_forward", FrontendKind.normalize("  UMH_FORWARD "))
        assertEquals(
            FrontendKind.RootChild,
            FrontendKind.resolve(FrontendKind.normalize("Root_Child")),
        )
    }

    @Test
    fun `step set resolve is exact and normalize is the boundary`() {
        assertEquals(StepSetKind.W1W2, StepSetKind.resolve("w1_w2"))
        assertEquals(StepSetKind.PAGE_CACHE_WRITE, StepSetKind.resolve("pagecache_write"))
        assertNull(StepSetKind.resolve("W1_W2"))
        assertNull(StepSetKind.resolve(" w1_w3 "))
        assertNull(StepSetKind.resolve(""))
        assertNull(StepSetKind.resolve(null))
        assertEquals("w1_w3", StepSetKind.normalize("  W1_W3 "))
        assertEquals(
            StepSetKind.W1W3,
            StepSetKind.resolve(StepSetKind.normalize(" W1_W3")),
        )
    }
}
