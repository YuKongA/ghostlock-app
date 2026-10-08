package com.ghostlock.app.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Guards for the read-only declaration projection (P0-1).
 *
 * The declared VALUES mirror the bundled A301SO profile
 * (app/src/main/assets/profile/5.15.189-android13-8-00016-g51bba4309aac-ab14546557.conf:5-11):
 *   43284 { queue = [ { step = "pagecache_write" } ] }
 *   43499 { route = "multicast_waiter", queue = [w1, w2, w3] }
 */
class DeclaredCombinationsTest {
    private fun declared(available: ValueMap) =
        declaredCombinations(valueMapOf("available" to available))

    private fun a301so() = valueMapOf(
        "cve_2026_43284" to valueMapOf(
            "queue" to listOf(valueMapOf("step" to "pagecache_write")),
        ),
        "cve_2026_43499" to valueMapOf(
            "route" to "multicast_waiter",
            "queue" to listOf(
                valueMapOf("step" to "w1"),
                valueMapOf("step" to "w2"),
                valueMapOf("step" to "w3"),
            ),
        ),
    )

    @Test
    fun theDeclaredValuesMatchTheA301soProfile() {
        val entries = declared(a301so())
        assertEquals("two declared backends", 2, entries.size)
        val mcast = entries.single { it.backend == "cve_2026_43499" }
        assertEquals("multicast_waiter", mcast.route)
        assertEquals(listOf("w1", "w2", "w3"), mcast.steps)
        assertNull("handoff is not declared yet", mcast.handoff)
        val pagecache = entries.single { it.backend == "cve_2026_43284" }
        assertNull("43284 has no route axis", pagecache.route)
        assertEquals(listOf("pagecache_write"), pagecache.steps)
    }

    @Test
    fun undeclaredRoutesAndBackendsNeverAppear() {
        val entries = declared(a301so())
        assertEquals("no extra backend", 2, entries.size)
        assertTrue(
            "select_stack is not declared and must not appear",
            entries.none { it.route == "select_stack" },
        )
        assertTrue(
            "tcp_zerocopy is not declared and must not appear",
            entries.none { it.route == "tcp_zerocopy" },
        )
    }

    @Test
    fun experimentalStillYieldsExactlyOneEntry() {
        val available = valueMapOf(
            "cve_2026_43499" to valueMapOf(
                "route" to "multicast_waiter",
                "queue" to listOf(valueMapOf("step" to "w1")),
                "experimental" to true,
            ),
        )
        assertEquals(1, declared(available).size)
    }

    @Test
    fun orderAndDefaultFollowTheDeclaredPriority() {
        val available = valueMapOf(
            "cve_2026_43284" to valueMapOf(
                "queue" to listOf(valueMapOf("step" to "pagecache_write")),
                "priority" to 2L,
            ),
            "cve_2026_43499" to valueMapOf(
                "route" to "multicast_waiter",
                "queue" to listOf(valueMapOf("step" to "w1")),
                "priority" to 1L,
            ),
            "cve_2026_64560" to valueMapOf(
                "queue" to listOf(valueMapOf("step" to "w1")),
            ),
        )
        val entries = declared(available)
        assertEquals(
            "smallest priority first, undeclared last",
            listOf("cve_2026_43499", "cve_2026_43284", "cve_2026_64560"),
            entries.map { it.backend },
        )
        assertEquals("the default is the smallest priority", "cve_2026_43499", entries.first().backend)
        assertEquals(1L, entries.first().priority)
        assertNull("no priority declared", entries.last().priority)
    }
}
