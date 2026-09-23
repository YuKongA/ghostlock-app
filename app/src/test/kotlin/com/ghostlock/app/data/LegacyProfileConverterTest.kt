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
    fun `upstream extractor report converts to current profile layout`() {
        val entry = valueMapOf(
            "release" to "6.1.118-test",
            "kimage_text_base" to 0x100000L,
            "btf_size" to 4096,
            "compact_waiter" to 1,
            "pselect_waiter_shift" to 1,
            "mm_struct_sz" to 0x400,
            "struct_fields" to valueMapOf("task_prio" to 132),
            "symbols" to valueMapOf("off_init_task" to 33420800L),
        )

        LegacyProfileConverter.convertValue(entry)

        assertFalse(entry.containsKey("kimage_text_base"))
        assertFalse(entry.containsKey("btf_size"))
        assertEquals(0x400, entry["kernelsnitch"].asValueMap()!!["mm_struct_sz"])
        assertEquals(132, entry["task_struct"].asValueMap()!!["prio"])
        assertEquals(33420800L, entry["offset"].asValueMap()!!["init_task"])
        assertEquals("tcp_zerocopy", entry["route"].asValueMap()!!.keys.first())
    }

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
    fun `imported 6x report gains kernel_major and shared defaults`() {
        val entry = valueMapOf(
            "release" to "6.12.38-android16-5-gbe6292a1543d-ab14525421-4k",
            "kernel_phys_load" to 3347054592L,
            "pselect_waiter_shift" to 0,
            "struct_fields" to valueMapOf("task_prio" to 148, "struct_mm_struct" to 1216),
            "offset" to valueMapOf("init_task" to 37801728L),
        )

        LegacyProfileConverter.convertValue(entry)

        assertEquals(6L, entry["kernel_major"])
        val cred = entry["cred"].asValueMap()!!
        assertEquals(136L, cred["copy_size"])
        assertEquals(48L, cred["caps_offset"])
        assertEquals(5L, cred["caps_count"])
        val snitch = entry["kernelsnitch"].asValueMap()!!
        assertEquals(4L, snitch["collisions"])
    }

    @Test
    fun `existing values are never overwritten by shared defaults`() {
        val entry = valueMapOf(
            "release" to "6.12.38-test",
            "kernel_major" to 6,
            "cred" to valueMapOf("copy_size" to 176, "caps_count" to 3),
        )

        LegacyProfileConverter.convertValue(entry)

        val cred = entry["cred"].asValueMap()!!
        assertEquals(176, cred["copy_size"])
        assertEquals(3, cred["caps_count"])
        assertEquals(48L, cred["caps_offset"])
    }

    @Test
    fun `5x report gets its major but no 6x credential template`() {
        val entry = valueMapOf("release" to "5.15.189-android13-8-test")

        LegacyProfileConverter.convertValue(entry)

        assertEquals(5L, entry["kernel_major"])
        assertFalse(entry.containsKey("cred"))
    }

    @Test
    fun `v2 profile is not seeded and keeps its missing fields`() {
        val entry = valueMapOf("release" to "6.12.38-test", "schema_version" to 1)

        LegacyProfileConverter.convertValue(entry)

        assertFalse(entry.containsKey("kernel_major"))
        assertFalse(entry.containsKey("cred"))
        assertFalse(entry.containsKey("kernelsnitch"))
    }

    @Test
    fun `sparse override without a release is not seeded`() {
        val entry = valueMapOf("kernel_major" to 6, "task_struct" to valueMapOf("prio" to 140))

        LegacyProfileConverter.convertValue(entry)

        assertFalse(entry.containsKey("cred"))
        assertFalse(entry.containsKey("kernelsnitch"))
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
