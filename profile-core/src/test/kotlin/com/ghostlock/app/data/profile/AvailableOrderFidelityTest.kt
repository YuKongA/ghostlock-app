package com.ghostlock.app.data.profile

import com.ghostlock.app.data.AvailablePriority
import com.ghostlock.app.data.HoconSupport
import com.ghostlock.app.data.ProfileLayout
import com.ghostlock.app.data.ValueMap
import com.ghostlock.app.data.asValueMap
import com.ghostlock.app.data.valueMapOf
import org.junit.Assert.assertEquals
import org.junit.Ignore
import org.junit.Test

/**
 * R5-ORDER-01 rewritten under design 2.8.A (user ruling A, 2026-10-06): the
 * selection criterion is the EXPLICIT `available.<backend>.priority`, NOT the
 * declaration order.
 *
 * Measured fact that forced the ruling (2026-10-06): the parse stage already
 * yields [cve_2026_43499, cve_2026_43284] for a declaration of
 * [cve_2026_43284, cve_2026_43499], and the canonical stage inherits it - so the
 * old "order is priority" rule was not implementable. The three assertions below
 * now check what 2.8.A actually promises: every stage keeps the declared PRIORITY
 * values, and the submenu default is the same regardless of declaration order.
 *
 * @Ignore stays until this batch lands (per the batch instruction): removing it
 * turns these into live guards.
 */
class AvailableOrderFidelityTest {

    /** 43284 declares the better (smaller) priority; the ORDER is deliberately
     * reversed against it, which is exactly the R5-ORDER-01 situation. */
    private val document = """
        ghostlock {
          schema_version = 3
          release = "r"
          kernel_major = 6
          available {
            cve_2026_43499 { priority = 9, route = "multicast_waiter", queue = [ { step = "w1" } ] }
            cve_2026_43284 { priority = 2, queue = [ { step = "pagecache_write" } ] }
          }
        }
    """.trimIndent()

    private fun parse(text: String): ValueMap =
        requireNotNull(HoconSupport.parseValue(text).asValueMap())

    private fun available(map: ValueMap): ValueMap =
        (map["ghostlock"].asValueMap() ?: map)["available"].asValueMap()!!

    private fun priorities(map: ValueMap): Map<String, Long?> {
        val available = available(map)
        return available.keys.filterIsInstance<String>().associateWith { backend ->
            AvailablePriority.priorityOf(backend, available[backend])
        }
    }

    @Ignore("R5-ORDER-01 rewritten under 2.8.A; un-ignore when this batch lands")
    @Test
    fun parseStageKeepsTheDeclaredPriorities() {
        val actual = priorities(parse(document))
        println("priority-parse actual=" + actual)
        assertEquals("the parse stage dropped a declared priority", 9L, actual["cve_2026_43499"])
        assertEquals("the parse stage dropped a declared priority", 2L, actual["cve_2026_43284"])
        assertEquals(
            "the default must come from the priority, not the declaration order",
            "cve_2026_43284",
            AvailablePriority.defaultBackend(available(parse(document))),
        )
    }

    @Ignore("R5-ORDER-01 rewritten under 2.8.A; un-ignore when this batch lands")
    @Test
    fun mergeStageKeepsTheDeclaredPriorities() {
        val merged = ProfileMerger.resolveMerged(
            deviceRelease = "r",
            builtin = parse(document),
            imported = null,
            overrides = null,
            tuningExecution = valueMapOf(),
            pair = CpuPairView(0, 1),
            routePresets = emptyMap(),
        )
        val actual = priorities(merged)
        println("priority-merge actual=" + actual)
        assertEquals("the merge stage dropped a declared priority", 9L, actual["cve_2026_43499"])
        assertEquals("the merge stage dropped a declared priority", 2L, actual["cve_2026_43284"])
        assertEquals(
            "the merge stage must not change the default",
            "cve_2026_43284",
            AvailablePriority.defaultBackend(available(merged)),
        )
    }

    @Ignore("R5-ORDER-01 rewritten under 2.8.A; un-ignore when this batch lands")
    @Test
    fun canonicalStageKeepsTheDeclaredPrioritiesAndTheDefault() {
        val canonical = ProfileLayout.canonicalize(parse(document))
        val actual = priorities(canonical)
        println("priority-canonical actual=" + actual)
        assertEquals("the canonical stage dropped a declared priority", 9L, actual["cve_2026_43499"])
        assertEquals("the canonical stage dropped a declared priority", 2L, actual["cve_2026_43284"])
        assertEquals(
            "cve_2026_43284",
            AvailablePriority.defaultBackend(available(canonical)),
        )
    }
}
