package com.ghostlock.app.ui

import com.ghostlock.app.data.plugin.PluginConfigValidator
import com.ghostlock.app.data.plugin.PluginManifestEntry
import com.ghostlock.app.data.plugin.PluginParamType
import com.ghostlock.app.data.plugin.PluginProbe
import com.ghostlock.app.data.plugin.PluginValue
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** P1 page projections: only what the contract allows can be offered. */
class PluginPresentationTest {

    private val sha = "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"

    private fun entry(id: String = "demo.plugin", enabled: Boolean = false) = PluginManifestEntry(
        id = id,
        version = "1.0",
        abiVersion = 1,
        sha256 = sha,
        modulePath = id + "/1.0/" + id + ".so",
        enabled = enabled,
        stage = "post_terminal",
        importedAtMs = 7L,
    )

    private fun descriptor(
        caps: String = "kernel_read",
        requiredParam: Boolean = false,
    ) = PluginProbe.parse(
        "host_abi\t1\n" +
            "countermeasures_root\tcountermeasures\n" +
            "host_stages\tpost_terminal\n" +
            "host_caps\tkernel_read,alias\n" +
            "plugin\tdemo.plugin\t1.0\t1\t64\t" + sha + "\tpost_terminal\t" + caps + "\n" +
            "param\tdemo.plugin\tarm_delay_us\tuint\t1\t200\thold\n" +
            (if (requiredParam) "param\tdemo.plugin\ttoken\tstr\t1\t-\tmust be set\n" else ""),
    )

    @Test
    fun `a row without a descriptor is greyed but still listed`() {
        val row = pluginRows(listOf(entry()), emptyMap()).single()
        assertEquals("demo.plugin", row.id)
        assertFalse(row.selectable)
        assertTrue(row.blockedReason!!.contains("native probe"))
        assertEquals("abi 1", row.summary)
    }

    @Test
    fun `a host-acceptable descriptor makes the row selectable`() {
        val row = pluginRows(listOf(entry()), mapOf("demo.plugin" to descriptor())).single()
        assertTrue(row.selectable)
        assertNull(row.blockedReason)
        assertTrue(row.summary.contains("post_terminal"))
        assertTrue(row.summary.contains("kernel_read"))
        assertEquals(sha.take(12), row.sha256Short)
    }

    @Test
    fun `a capability the host lacks greys the row with the reason`() {
        val row = pluginRows(
            listOf(entry()),
            mapOf("demo.plugin" to descriptor(caps = "kernel_read,kernel_hook")),
        ).single()
        assertFalse(row.selectable)
        assertTrue(row.blockedReason!!.contains("kernel_hook"))
    }

    @Test
    fun `param rows follow the declared order and carry defaults and errors`() {
        val plugin = descriptor(requiredParam = true)
        val errors = PluginConfigValidator.validate(
            plugin,
            enabled = true,
            stage = "post_terminal",
            overrides = mapOf("arm_delay_us" to PluginValue.UInt(5u)),
        )
        val rows = pluginParamRows(plugin, mapOf("arm_delay_us" to PluginValue.UInt(5u)), errors)
        assertEquals(listOf("arm_delay_us", "token"), rows.map { it.name })
        assertEquals(PluginParamType.UInt, rows[0].type)
        assertEquals(PluginParamEditor.Text, rows[0].editor)
        assertEquals("5", rows[0].value)
        assertEquals("200", rows[0].defaultText)
        assertNull(rows[0].error)
        assertEquals("", rows[1].value)
        assertNull(rows[1].defaultText)
        assertTrue(rows[1].required)
        assertTrue(rows[1].error!!.contains("required"))
        assertEquals("plugin.demo.plugin.params.token", rows[1].path)
    }

    @Test
    fun `a bool parameter renders as a switch, every other kind as text`() {
        val plugin = PluginProbe.parse(
            "host_abi\t1\n" +
                "countermeasures_root\tcountermeasures\n" +
                "host_stages\tpost_terminal\n" +
                "host_caps\tkernel_read\n" +
                "plugin\tdemo.plugin\t1.0\t1\t64\t" + sha + "\tpost_terminal\tkernel_read\n" +
                "param\tdemo.plugin\tflag\tbool\t0\t1\tdoc\n" +
                "param\tdemo.plugin\tthreshold\tuint\t0\t200\tdoc\n" +
                "param\tdemo.plugin\tmode\tstr\t0\tauto\tdoc\n" +
                "param\tdemo.plugin\tdelta\tint\t0\t0\tdoc\n",
        )
        val rows = pluginParamRows(plugin, emptyMap(), emptyList())
        assertEquals(PluginParamEditor.Switch, rows.first { it.name == "flag" }.editor)
        assertEquals(PluginParamEditor.Text, rows.first { it.name == "threshold" }.editor)
        assertEquals(PluginParamEditor.Text, rows.first { it.name == "mode" }.editor)
        assertEquals(PluginParamEditor.Text, rows.first { it.name == "delta" }.editor)
    }

    @Test
    fun `param summaries show type, requirement, default and error`() {
        val plugin = descriptor(requiredParam = true)
        val errors = PluginConfigValidator.validate(
            plugin,
            enabled = true,
            stage = "post_terminal",
            overrides = emptyMap(),
        )
        val rows = pluginParamRows(plugin, emptyMap(), errors)
        assertEquals("arm_delay_us (uint, required) — 200", pluginParamSummary(rows[0]))
        assertTrue(pluginParamSummary(rows[1]).startsWith("token (str, required)"))
        assertTrue(pluginParamSummary(rows[1]).contains("required parameter"))
    }

    @Test
    fun `value text round-trips every parameter type`() {
        assertEquals("7", pluginValueText(PluginValue.UInt(7u)))
        assertEquals("-3", pluginValueText(PluginValue.Int(-3L)))
        assertEquals("true", pluginValueText(PluginValue.Bool(true)))
        assertEquals("abc", pluginValueText(PluginValue.Str("abc")))
    }
}
