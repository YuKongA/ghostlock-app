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
            "param\tdemo.plugin\tmode\tstr\t0\tauto\tdoc\n",
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
