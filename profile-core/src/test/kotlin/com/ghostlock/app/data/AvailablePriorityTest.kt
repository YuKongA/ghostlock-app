package com.ghostlock.app.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Design 2.8.A (user ruling A): `available.<backend>.priority` is an EXPLICIT
 * positive integer that replaces the implicit "declaration order is priority"
 * rule. Smallest = highest priority; absent = lowest (sorts last); the unselected
 * submenu default is the first entry of that order; equal explicit priorities are a
 * configuration error (fail closed - design 2.8 defines no tie-break).
 *
 * R5-ORDER-01 measured that the parse stage reorders `available` keys, which is
 * exactly why the criterion moved off declaration order; the order-independence
 * case below is the rewritten form of that intent.
 */
class AvailablePriorityTest {

    private fun parse(text: String): ValueMap =
        requireNotNull(HoconSupport.parseValue(text).asValueMap())

    private fun document(declarations: String): ValueMap = parse(
        """
        ghostlock {
          schema_version = 3
          release = "r"
          kernel_major = 6
          available {
        $declarations
          }
        }
        """.trimIndent(),
    )

    private val high = """
        cve_2026_43499 { priority = 1, route = "multicast_waiter", queue = [ { step = "w1" } ] }
        cve_2026_43284 { priority = 2, queue = [ { step = "pagecache_write" } ] }
    """.trimIndent()

    private val highSwapped = """
        cve_2026_43284 { priority = 2, queue = [ { step = "pagecache_write" } ] }
        cve_2026_43499 { priority = 1, route = "multicast_waiter", queue = [ { step = "w1" } ] }
    """.trimIndent()

    private fun available(map: ValueMap): ValueMap =
        (map["ghostlock"].asValueMap() ?: map)["available"].asValueMap()!!

    @Test
    fun `a declared priority is accepted by canonical validation`() {
        /* Before 2.8.A this threw "available.cve_2026_43499.priority: unknown key". */
        val canonical = ProfileLayout.canonicalize(document(high))
        val available = available(canonical)
        assertEquals(1L, AvailablePriority.priorityOf("cve_2026_43499", available["cve_2026_43499"]))
        assertEquals(2L, AvailablePriority.priorityOf("cve_2026_43284", available["cve_2026_43284"]))
    }

    @Test
    fun `priority is presentation-only and never reaches the backend owner`() {
        val canonical = ProfileLayout.canonicalize(document(high))
        val owners = canonical["backend"].asValueMap()!!
        for (token in listOf("cve_2026_43499", "cve_2026_43284")) {
            val owner = owners[token].asValueMap()
            assertTrue(
                "backend.$token must not carry the presentation-only priority",
                owner == null || !owner.containsKey(AvailablePriority.Key),
            )
        }
    }

    @Test
    fun `the default is the smallest priority`() {
        assertEquals("cve_2026_43499", AvailablePriority.defaultBackend(available(document(high))))
    }

    @Test
    fun `the default does not depend on the declaration order`() {
        /* R5-ORDER-01 rewritten under 2.8.A: the ORDER may be reordered by the
         * parser, the PRIORITY may not be. */
        assertEquals(
            AvailablePriority.defaultBackend(available(document(high))),
            AvailablePriority.defaultBackend(available(document(highSwapped))),
        )
        assertEquals(
            listOf("cve_2026_43499", "cve_2026_43284"),
            AvailablePriority.orderedBackends(available(document(highSwapped))),
        )
    }

    @Test
    fun `an entry without a priority sorts last`() {
        val mixed = document(
            """
            cve_2026_43284 { queue = [ { step = "pagecache_write" } ] }
            cve_2026_43499 { priority = 7, route = "multicast_waiter", queue = [ { step = "w1" } ] }
            """.trimIndent(),
        )
        assertEquals(
            listOf("cve_2026_43499", "cve_2026_43284"),
            AvailablePriority.orderedBackends(available(mixed)),
        )
        assertEquals("cve_2026_43499", AvailablePriority.defaultBackend(available(mixed)))
    }

    @Test
    fun `equal priorities are a configuration error`() {
        val tied = document(
            """
            cve_2026_43499 { priority = 3, route = "multicast_waiter", queue = [ { step = "w1" } ] }
            cve_2026_43284 { priority = 3, queue = [ { step = "pagecache_write" } ] }
            """.trimIndent(),
        )
        /* Validation itself accepts the value; the ORDER authority refuses to pick
         * a winner (design 2.8 defines no tie-break). */
        val available = available(ProfileLayout.canonicalize(tied))
        val failure = assertThrows(IllegalArgumentException::class.java) {
            AvailablePriority.orderedBackends(available)
        }
        assertTrue(
            "the refusal must name the tie: " + failure.message,
            failure.message!!.contains("declared by both"),
        )
    }

    @Test
    fun `a non-positive or fractional priority fails closed`() {
        for (bad in listOf("0", "-1", "1.5", "\"high\"")) {
            val failure = assertThrows(IllegalArgumentException::class.java) {
                ProfileLayout.canonicalize(
                    document(
                        """
                        cve_2026_43499 { priority = $bad, route = "multicast_waiter", queue = [ { step = "w1" } ] }
                        """.trimIndent(),
                    ),
                )
            }
            assertTrue(
                "priority=$bad must be refused: " + failure.message,
                failure.message!!.contains("must be a positive integer"),
            )
        }
    }

    @Test
    fun `the selected backend is derived from the declaration when no preference is set`() {
        /* Design 2.9/U17: the declaration IS the selection surface. */
        val only43284 = valueMapOf(
            "cve_2026_43284" to valueMapOf("queue" to listOf(valueMapOf("step" to "pagecache_write"))),
        )
        assertEquals(
            "a 43284-only declaration must select 43284",
            "cve_2026_43284",
            AvailablePriority.selectedBackend(valueMapOf("available" to only43284), null),
        )
        /* Priority decides among several declarations (the same authority). */
        assertEquals(
            "cve_2026_43499",
            AvailablePriority.selectedBackend(valueMapOf("available" to available(document(high))), null),
        )
        /* An explicit preference still wins (design 1-prime). */
        assertEquals(
            "cve_2026_43284",
            AvailablePriority.selectedBackend(
                valueMapOf("available" to available(document(high))),
                "cve_2026_43284",
            ),
        )
    }

    @Test
    fun `the runtime carrier and backend_kind are accepted as fallbacks`() {
        val runtime = valueMapOf(
            "backend" to valueMapOf(
                "kind" to "cve_2026_43284",
                ProfileLayout.QueueSelectionKey to valueMapOf(
                    "cve_2026_43284" to valueMapOf("queue" to listOf(valueMapOf("step" to "w1"))),
                ),
            ),
        )
        assertEquals("cve_2026_43284", AvailablePriority.selectedBackend(runtime, null))
        val kindOnly = valueMapOf("backend" to valueMapOf("kind" to "cve_2026_43284"))
        assertEquals("cve_2026_43284", AvailablePriority.selectedBackend(kindOnly, null))
        assertNull("nothing to derive from must stay null", AvailablePriority.selectedBackend(null, null))
    }

    @Test
    fun `nothing declared means no default`() {
        assertNull(AvailablePriority.defaultBackend(null))
        assertNull(AvailablePriority.defaultBackend(valueMapOf()))
    }
}
