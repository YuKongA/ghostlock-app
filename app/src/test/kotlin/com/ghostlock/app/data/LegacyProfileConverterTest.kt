package com.ghostlock.app.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Covers the transitions that previously lost data: the tcp report keeping its
 * pselect shift as an explicit select fallback, idempotent conversion and the
 * empty branch objects old builds wrote when switching routes.
 */
class LegacyProfileConverterTest {

    @Test
    fun `tcp report keeps pselect as explicit select fallback`() {
        val entry = valueMapOf(
            "release" to "r",
            "kernel_major" to 6,
            "compact_waiter" to 1,
            "pselect_waiter_shift" to 1,
            "struct_fields" to valueMapOf("task_prio" to 132),
            "symbols" to valueMapOf("off_init_task" to 33420800L),
        )

        LegacyProfileConverter.convertValue(entry)

        assertEquals("tcp_zerocopy", entry["route"].asValueMap()!!.keys.first())
        assertEquals("select_stack", entry["fallback"].asValueMap()!!["to"])
        assertEquals(
            1,
            entry["fallback"].asValueMap()!!["route"].asValueMap()!!
                ["select_stack"].asValueMap()!!["waiter_shift"],
        )
        assertEquals(132, entry["task_struct"].asValueMap()!!["prio"])
        assertEquals(33420800L, entry["offset"].asValueMap()!!["init_task"])
    }

    @Test
    fun `select report without pselect has no fallback and no empty branch`() {
        val entry = valueMapOf(
            "release" to "r",
            "kernel_major" to 6,
            "struct_fields" to valueMapOf("task_prio" to 132),
        )

        LegacyProfileConverter.convertValue(entry)

        /* The inferred select branch carries no fields, so it is dropped and
         * the route falls back to inference (which resolves to select). */
        assertFalse(entry.containsKey("route"))
        assertEquals("none", entry["fallback"].asValueMap()!!["to"])
    }

    @Test
    fun `conversion is idempotent`() {
        val entry = valueMapOf(
            "release" to "r",
            "kernel_major" to 6,
            "compact_waiter" to 1,
            "pselect_waiter_shift" to 1,
        )

        LegacyProfileConverter.convertValue(entry)
        val once = entry.toString()
        LegacyProfileConverter.convertValue(entry)

        assertEquals(once, entry.toString())
    }

    @Test
    fun `empty route branch from old switches is dropped`() {
        val entry = valueMapOf(
            "release" to "r",
            "kernel_major" to 5,
            "route" to valueMapOf("select_stack" to valueMapOf()),
        )

        LegacyProfileConverter.convertValue(entry)

        assertFalse(entry.containsKey("route"))
    }

    @Test
    fun `seeded branch keeps its fields`() {
        val entry = valueMapOf(
            "release" to "r",
            "kernel_major" to 6,
            "route" to valueMapOf("select_stack" to valueMapOf("waiter_shift" to null)),
        )

        LegacyProfileConverter.convertValue(entry)

        assertTrue(entry.containsKey("route"))
        assertTrue(entry["route"].asValueMap()!!["select_stack"].asValueMap()!!.containsKey("waiter_shift"))
    }
}

class SparseOverrideConversionTest {
    @Test
    fun `empty override map stays empty`() {
        val entry = valueMapOf()
        LegacyProfileConverter.convertValue(entry)
        assertTrue("empty override must not gain a route", !entry.containsKey("route"))
        assertTrue("empty override must not gain a fallback", !entry.containsKey("fallback"))
    }

    @Test
    fun `sparse override keeps only its own fields`() {
        val entry = valueMapOf("task_struct" to valueMapOf("prio" to 140))
        LegacyProfileConverter.convertValue(entry)
        assertFalse(entry.containsKey("route"))
        assertFalse(entry.containsKey("fallback"))
        assertEquals(140, entry["task_struct"].asValueMap()!!["prio"])
    }
}
