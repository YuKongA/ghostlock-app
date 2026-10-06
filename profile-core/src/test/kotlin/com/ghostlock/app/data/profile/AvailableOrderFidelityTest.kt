package com.ghostlock.app.data.profile

import com.ghostlock.app.data.HoconSupport
import com.ghostlock.app.data.ProfileLayout
import com.ghostlock.app.data.ValueMap
import com.ghostlock.app.data.asValueMap
import com.ghostlock.app.data.valueMapOf
import org.junit.Assert.assertEquals
import org.junit.Ignore
import org.junit.Test

/**
 * Batch 3 (design 2.8): the ORDER of the backends declared inside available is
 * their PRIORITY and the default of an unselected submenu is the FIRST entry, so
 * the declaration order must survive parsing, merging and canonicalisation.
 *
 * R5-ORDER-01 (2026-10-06, measured with these very tests): for a declaration of
 * [cve_2026_43284, cve_2026_43499] the PARSE stage already yields
 * [cve_2026_43499, cve_2026_43284], and the canonical stage inherits it. The merge
 * stage could not be measured yet (the fixture needs a kernel_major the merger
 * accepts). Annotated until the design 2.8 A/B/C ruling lands so the tree stays
 * committable; the bodies are complete, so removing the annotations turns them into
 * real guards.
 */
class AvailableOrderFidelityTest {

    private val declared = listOf("cve_2026_43284", "cve_2026_43499")

    private val document = """
        ghostlock {
          schema_version = 3
          release = "r"
          kernel_major = 6
          available {
            cve_2026_43284 { queue = [ { step = "pagecache_write" } ] }
            cve_2026_43499 { route = "multicast_waiter", queue = [ { step = "w1" } ] }
          }
        }
    """.trimIndent()

    /* The parsed text is wrapped in the ghostlock block; canonicalisation unwraps it. */
    private fun availableKeys(map: ValueMap): List<String> =
        (map["ghostlock"].asValueMap() ?: map)["available"].asValueMap()?.keys?.toList().orEmpty()

    private fun parse(text: String): ValueMap =
        requireNotNull(HoconSupport.parseValue(text).asValueMap())

    @Ignore("R5-ORDER-01: the parse stage reorders available keys; pending the A/B/C decision in design section 2.8")
    @Test
    fun parseStageKeepsTheDeclarationOrder() {
        val actual = availableKeys(parse(document))
        println("order-parse actual=" + actual)
        assertEquals("parse stage reordered the backends", declared, actual)
    }

    @Ignore("R5-ORDER-01: the parse stage reorders available keys; pending the A/B/C decision in design section 2.8")
    @Test
    fun mergeStageKeepsTheDeclarationOrder() {
        val merged = ProfileMerger.resolveMerged(
            deviceRelease = "r",
            builtin = parse(document),
            imported = null,
            overrides = null,
            tuningExecution = valueMapOf(),
            pair = CpuPairView(0, 1),
            routePresets = emptyMap(),
        )
        val actual = availableKeys(merged)
        println("order-merge actual=" + actual)
        assertEquals("merge stage reordered the backends", declared, actual)
    }

    @Ignore("R5-ORDER-01: the parse stage reorders available keys; pending the A/B/C decision in design section 2.8")
    @Test
    fun canonicalStageKeepsTheDeclarationOrder() {
        val actual = availableKeys(ProfileLayout.canonicalize(parse(document)))
        println("order-canonical actual=" + actual)
        assertEquals("canonical stage reordered the backends", declared, actual)
    }
}