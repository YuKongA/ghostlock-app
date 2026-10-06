package com.ghostlock.app.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * M2 dual shape of `available.<backend>` (design doc 4.5 / 5-Q1-Q3 / 5-Q2).
 *
 * The legacy token LIST stays accepted and adds no backend key; the OBJECT form
 * declares the queue-level route, the step queue and the static experimental
 * opt-in, and [ProfileLayout.canonicalize] carries exactly those three into the
 * backend owner the wire reads — never overwriting the per-route geometry map
 * that already owns `backend.<id>.route` (the route string rides `queue_route`
 * there, and the adapter maps it back onto the wire key `route`).
 */
class ProfileLayoutAvailableTest {

    @Test
    fun `the token list form is accepted unchanged and adds no backend key`() {
        val canonical = ProfileLayout.canonicalize(
            document("cve_2026_43499" to listOf("mcast_rootchild")),
        )
        assertEquals(
            listOf("mcast_rootchild"),
            canonical["available"].asValueMap()?.get("cve_2026_43499"),
        )
        /* Zero new bytes: the list form carries no queue selection, so the
         * backend owner keeps exactly the keys the document declared. */
        assertEquals(setOf("steps"), owner(canonical).keys)
    }

    @Test
    fun `the object form is carried into the backend owner`() {
        val canonical = ProfileLayout.canonicalize(
            document(
                "cve_2026_43499" to valueMapOf(
                    "route" to "Multicast_Waiter",
                    "queue" to listOf(valueMapOf("step" to "w1")),
                    "experimental" to true,
                ),
            ),
        )
        val carried = owner(canonical)
        assertEquals("the route token is stored normalized", "multicast_waiter", carried["route"])
        assertEquals(listOf(valueMapOf("step" to "w1")), carried["queue"])
        assertEquals(true, carried["experimental"])
        assertEquals("mcast_rootchild", carried["steps"])
    }

    @Test
    fun `the route geometry map is never overwritten and the token rides queue_route`() {
        val geometry = valueMapOf("multicast_waiter" to valueMapOf("waiter_off" to -2))
        val canonical = ProfileLayout.canonicalize(
            document(
                "cve_2026_43499" to valueMapOf(
                    "route" to "multicast_waiter",
                    "queue" to listOf(valueMapOf("step" to "w1")),
                    "experimental" to true,
                ),
                backendExtra = valueMapOf("route" to geometry),
            ),
        )
        val carried = owner(canonical)
        assertEquals("the geometry map must be untouched", geometry, carried["route"])
        assertEquals("multicast_waiter", carried["queue_route"])
        assertEquals(true, carried["experimental"])
    }

    @Test
    fun `a backend without geometry takes the queue route directly`() {
        val canonical = ProfileLayout.canonicalize(
            document(
                "cve_2026_43284" to valueMapOf(
                    "queue" to listOf(valueMapOf("seam" to "plugin", "stage" to "post_terminal")),
                    "experimental" to true,
                ),
                backend = "cve_2026_43284",
            ),
        )
        val carried = owner(canonical, "cve_2026_43284")
        assertEquals(
            listOf(valueMapOf("seam" to "plugin", "stage" to "post_terminal")),
            carried["queue"],
        )
        assertEquals(true, carried["experimental"])
        /* No route axis: nothing may appear under `route`. */
        assertEquals(null, carried["route"])
    }

    @Test
    fun `canonicalize is idempotent for both shapes`() {
        val selection = valueMapOf(
            "route" to "multicast_waiter",
            "queue" to listOf(valueMapOf("step" to "w1")),
            "experimental" to true,
        )
        val documents = listOf(
            document("cve_2026_43499" to listOf("mcast_rootchild")),
            document("cve_2026_43499" to selection),
            document(
                "cve_2026_43499" to selection,
                backendExtra = valueMapOf(
                    "route" to valueMapOf("multicast_waiter" to valueMapOf("waiter_off" to -2)),
                ),
            ),
        )
        for (source in documents) {
            val once = ProfileLayout.canonicalize(source)
            assertTrue(ProfileLayout.isCanonical(once))
            assertEquals("canonicalize must be idempotent", once, ProfileLayout.canonicalize(once))
        }
    }

    @Test
    fun `malformed queue elements fail closed with their dotted path`() {
        val cases = listOf<Pair<Any?, String>>(
            listOf("w1") to "available.cve_2026_43499.queue[0]: queue-element-not-object",
            listOf(valueMapOf("step" to "w1", "seam" to "plugin")) to
                "queue-element-needs-exactly-one-of-step-seam",
            listOf(valueMapOf("stage" to "post_terminal")) to
                "queue-element-needs-exactly-one-of-step-seam",
            listOf(valueMapOf("step" to "w1", "unknown" to 1)) to
                "available.cve_2026_43499.queue[0].unknown: unknown key",
            listOf(valueMapOf("params" to valueMapOf("x" to 1))) to
                "params-reserved-for-future-step-parameters",
            listOf(valueMapOf("step" to "w1", "stage" to "post_terminal")) to
                "queue[0].stage: stage-requires-seam",
            listOf(valueMapOf("step" to 42)) to "queue[0].step: value must be a string",
            emptyList<Any?>() to "queue must be a non-empty list",
        )
        for ((queue, expected) in cases) {
            val thrown = assertThrows(IllegalArgumentException::class.java) {
                ProfileLayout.canonicalize(
                    document("cve_2026_43499" to valueMapOf("queue" to queue)),
                )
            }
            assertTrue(
                "expected [$expected] in [${thrown.message}]",
                thrown.message.orEmpty().contains(expected),
            )
        }
    }

    @Test
    fun `unknown selection keys, routes and experimental shapes fail closed`() {
        val cases = listOf<Pair<ValueMap, String>>(
            valueMapOf("nope" to 1) to "available.cve_2026_43499.nope: unknown key",
            valueMapOf("route" to "no_such_route") to
                "available.cve_2026_43499.route: not a known route",
            valueMapOf("experimental" to "yes") to
                "available.cve_2026_43499.experimental: must be a boolean",
        )
        for ((selection, expected) in cases) {
            val thrown = assertThrows(IllegalArgumentException::class.java) {
                ProfileLayout.canonicalize(document("cve_2026_43499" to selection))
            }
            assertTrue(
                "expected [$expected] in [${thrown.message}]",
                thrown.message.orEmpty().contains(expected),
            )
        }
    }

    @Test
    fun `two string route slots fail closed instead of picking a precedence`() {
        val thrown = assertThrows(IllegalArgumentException::class.java) {
            ProfileLayout.canonicalize(
                document(
                    "cve_2026_43499" to valueMapOf("route" to "multicast_waiter"),
                    backendExtra = valueMapOf(
                        "route" to "multicast_waiter",
                        "queue_route" to "multicast_waiter",
                    ),
                ),
            )
        }
        assertTrue(
            thrown.message.orEmpty(),
            thrown.message.orEmpty().contains("refusing to pick a precedence"),
        )
    }

    @Test
    fun `a backend selection key without an available declaration is an unknown key`() {
        val source = document("cve_2026_43499" to listOf("mcast_rootchild"))
        val backend = source["backend"].asValueMap()!!["cve_2026_43499"].asValueMap()!!
        backend["queue"] = listOf(valueMapOf("step" to "w1"))
        val thrown = assertThrows(IllegalArgumentException::class.java) {
            ProfileLayout.canonicalize(source)
        }
        assertTrue(
            thrown.message.orEmpty(),
            thrown.message.orEmpty().contains("backend.cve_2026_43499.queue: unknown profile key"),
        )
    }

    @Test
    fun `an echo that disagrees with the declaration is rejected`() {
        val thrown = assertThrows(IllegalArgumentException::class.java) {
            ProfileLayout.canonicalize(
                document(
                    "cve_2026_43499" to valueMapOf(
                        "route" to "multicast_waiter",
                        "experimental" to true,
                    ),
                    backendExtra = valueMapOf("experimental" to false),
                ),
            )
        }
        assertTrue(
            thrown.message.orEmpty(),
            thrown.message.orEmpty().contains("backend.cve_2026_43499.experimental"),
        )
    }

    private fun document(
        available: Pair<String, Any?>,
        backend: String = "cve_2026_43499",
        backendExtra: ValueMap = ValueMap(),
    ): ValueMap {
        val owner = ValueMap()
        owner["steps"] = if (backend == "cve_2026_43284") "umh" else "mcast_rootchild"
        owner.putAll(backendExtra)
        return valueMapOf(
            "schema_version" to 3,
            "release" to "5.15.189-test",
            "kernel_major" to 5,
            "available" to valueMapOf(available),
            "backend" to valueMapOf(backend to owner),
        )
    }

    private fun owner(canonical: ValueMap, backend: String = "cve_2026_43499"): ValueMap =
        checkNotNull(canonical["backend"].asValueMap()?.get(backend).asValueMap()) {
            "no backend owner for " + backend
        }
}
