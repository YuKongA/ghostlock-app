package com.ghostlock.app.data.plugin

import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test

/** P1 override extraction: descriptor types decide, mismatches fail closed. */
class PluginOverridesTest {

    private val descriptor = PluginProbe.parse(
        "host_abi\t1\n" +
            "countermeasures_root\tcountermeasures\n" +
            "host_stages\tpost_terminal\n" +
            "host_caps\tkernel_read\n" +
            "plugin\tdemo.plugin\t1.0\t1\t64\t" + "a".repeat(64) +
            "\tpost_terminal\tkernel_read\n" +
            "param\tdemo.plugin\tthreshold\tuint\t0\t200\tdoc\n" +
            "param\tdemo.plugin\tdelta\tint\t0\t0\tdoc\n" +
            "param\tdemo.plugin\tflag\tbool\t0\t1\tdoc\n" +
            "param\tdemo.plugin\tmode\tstr\t0\tauto\tdoc\n" +
            "extract\tdemo.plugin\toffset\tuint\t1\t-\tfrom the boot image\n" +
            "extract\tdemo.plugin\tsymbol\tstr\t1\t-\tfrom kallsyms\n",
    )

    private fun tree(params: Map<String, Any?>): Map<String, Any?> =
        mapOf("plugin" to mapOf("demo.plugin" to mapOf("params" to params)))

    @Test
    fun `stored overrides are read as their declared types`() {
        val values = PluginOverrides.params(
            tree(
                mapOf(
                    "threshold" to 7L,
                    "delta" to -3L,
                    "flag" to true,
                    "mode" to "manual",
                ),
            ),
            "demo.plugin",
            descriptor,
        )
        assertEquals(PluginValue.UInt(7u), values["threshold"])
        assertEquals(PluginValue.Int(-3L), values["delta"])
        assertEquals(PluginValue.Bool(true), values["flag"])
        assertEquals(PluginValue.Str("manual"), values["mode"])
        assertEquals("plugin.demo.plugin.params.mode", PluginOverrides.path("demo.plugin", "mode"))
    }

    @Test
    fun `extractor values are read from their own group with declared types`() {
        val tree = mapOf(
            "plugin" to mapOf(
                "demo.plugin" to mapOf(
                    "extract" to mapOf("offset" to 4096L, "symbol" to "task_defex_enforce"),
                ),
            ),
        )
        val values = PluginOverrides.extract(tree, "demo.plugin", descriptor)
        assertEquals(PluginValue.UInt(4096u), values["offset"])
        assertEquals(PluginValue.Str("task_defex_enforce"), values["symbol"])
        assertEquals(
            "plugin.demo.plugin.extract.offset",
            PluginOverrides.extractPath("demo.plugin", "offset"),
        )
        /* params and extract never bleed into each other. */
        assertTrue(PluginOverrides.params(tree, "demo.plugin", descriptor).isEmpty())
        val paramsTree = tree(mapOf("offset" to 1L))
        assertTrue(PluginOverrides.extract(paramsTree, "demo.plugin", descriptor).isEmpty())
    }

    @Test
    fun `an undeclared or mistyped extractor value fails closed`() {
        fun extract(values: Map<String, Any?>): Map<String, PluginValue> = PluginOverrides.extract(
            mapOf("plugin" to mapOf("demo.plugin" to mapOf("extract" to values))),
            "demo.plugin",
            descriptor,
        )
        assertTrue(extract(emptyMap()).isEmpty())
        assertThrows(IllegalArgumentException::class.java) { extract(mapOf("mystery" to 1L)) }
        assertThrows(IllegalArgumentException::class.java) { extract(mapOf("offset" to "4096")) }
        assertThrows(IllegalArgumentException::class.java) { extract(mapOf("symbol" to 7L)) }
    }

    @Test
    fun `an absent section means no overrides`() {
        assertTrue(PluginOverrides.params(emptyMap<String, Any?>(), "demo.plugin", descriptor).isEmpty())
        assertTrue(
            PluginOverrides.params(mapOf<String, Any?>("other" to 1L), "demo.plugin", descriptor)
                .isEmpty(),
        )
    }

    @Test
    fun `a value that cannot be read as the declared type fails closed`() {
        assertThrows(IllegalArgumentException::class.java) {
            PluginOverrides.params(tree(mapOf("threshold" to "7")), "demo.plugin", descriptor)
        }
        assertThrows(IllegalArgumentException::class.java) {
            PluginOverrides.params(tree(mapOf("threshold" to -1L)), "demo.plugin", descriptor)
        }
        assertThrows(IllegalArgumentException::class.java) {
            PluginOverrides.params(tree(mapOf("mode" to 7L)), "demo.plugin", descriptor)
        }
        /* A parameter the descriptor does not declare is refused, not ignored. */
        val error = assertThrows(IllegalArgumentException::class.java) {
            PluginOverrides.params(tree(mapOf("mystery" to 1L)), "demo.plugin", descriptor)
        }
        assertTrue(error.message!!.contains("mystery"))
    }
}
