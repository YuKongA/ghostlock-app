package com.ghostlock.app.ui

import com.ghostlock.app.R
import com.ghostlock.app.data.component.BackendKind
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

    /** Words distinct from the English defaults: only injection can spell them. */
    private val words = PluginTexts(
        required = "REQ",
        unresolved = "UNRES",
        defaultLadder = "LADDER",
    )

    private fun entry(id: String = "demo.plugin", enabled: Boolean = false) = PluginManifestEntry(
        id = id,
        version = "1.0",
        abiVersion = 1u,
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
    fun `a stage the selected backend cannot run is noted from the probe matrix only`() {
        /* The matrix is native data: the App only looks the selected backend up. */
        val described = PluginProbe.parse(
            "host_abi\t1\n" +
                "countermeasures_root\tcountermeasures\n" +
                "host_stages\tpre_spawn,post_spawn,pre_terminal,post_terminal\n" +
                "host_caps\tkernel_read,alias\n" +
                "stage_availability\t43499:pre_terminal;43284:post_terminal\n" +
                "plugin\tdemo.plugin\t1.0\t1\t64\t" + sha + "\tpost_terminal\tkernel_read\n",
        )
        val rows = mapOf("demo.plugin" to described)
        val on43499 = pluginRows(listOf(entry()), rows, BackendKind.Cve2026_43499).single()
        val note = requireNotNull(on43499.stageNote) { "the stage note must be present" }
        assertEquals(R.string.plugin_issue_stage_unavailable, note.resId)
        assertTrue(note.args.contains("post_terminal"))
        assertTrue(note.args.contains("cve_2026_43499"))
        assertTrue(note.args.contains("pre_terminal"))
        /* The very same plugin is fine on the backend that implements the stage. */
        assertNull(
            pluginRows(listOf(entry()), rows, BackendKind.Cve2026_43284).single().stageNote,
        )
        /* No matrix, no selected backend or no configured stage: no note. */
        assertNull(pluginRows(listOf(entry()), mapOf("demo.plugin" to descriptor()), null).single().stageNote)
        assertNull(
            pluginRows(listOf(entry()), mapOf("demo.plugin" to descriptor()), BackendKind.Cve2026_43499)
                .single().stageNote,
        )
        assertNull(
            pluginRows(listOf(entry().copy(stage = null)), rows, BackendKind.Cve2026_43499)
                .single().stageNote,
        )
    }

    @Test
    fun `run selection defaults to every enabled plugin and honours an explicit set`() {
        val enabledRow = entry(id = "alpha.plugin", enabled = true)
        val disabledRow = entry(id = "beta.plugin", enabled = false)
        val rows = listOf(enabledRow, disabledRow)
        fun selected(selection: Set<String>?): List<String> =
            pluginRows(rows, emptyMap(), null, selection).filter { it.selected }.map { it.id }

        /* Default: every enabled plugin, never a disabled one. */
        assertEquals(listOf("alpha.plugin"), selected(null))
        assertEquals(listOf("alpha.plugin"), selected(setOf("alpha.plugin")))
        /* An explicit empty set is "load nothing this run". */
        assertTrue(selected(emptySet()).isEmpty())
        /* Naming a disabled plugin does not select it. */
        assertTrue(selected(setOf("beta.plugin")).isEmpty())
    }

    @Test
    fun `a row without a descriptor is run-unusable but still toggleable`() {
        val row = pluginRows(listOf(entry()), emptyMap()).single()
        assertEquals("demo.plugin", row.id)
        assertFalse(row.runUsable)
        /* P0 UX: an installed row is ALWAYS switchable — otherwise disabling a
         * plugin (which drops its fresh description) would lock the user out. */
        assertTrue(row.toggleable)
        assertEquals(R.string.plugin_issue_no_descriptor, row.blockedReason!!.resId)
        assertEquals(
            listOf(PluginLine(R.string.plugin_summary_abi, listOf("1"))),
            row.summary,
        )
    }

    @Test
    fun `a host-acceptable descriptor makes the row run-usable`() {
        val row = pluginRows(listOf(entry()), mapOf("demo.plugin" to descriptor())).single()
        assertTrue(row.runUsable)
        assertTrue(row.toggleable)
        assertNull(row.blockedReason)
        assertEquals(
            listOf(
                PluginLine(R.string.plugin_summary_abi, listOf("1")),
                PluginLine(R.string.plugin_summary_stages, listOf("post_terminal")),
                PluginLine(R.string.plugin_summary_caps, listOf("kernel_read")),
            ),
            row.summary,
        )
        assertEquals(sha.take(12), row.sha256Short)
    }

    @Test
    fun `a capability the host lacks greys the row with the reason`() {
        val row = pluginRows(
            listOf(entry()),
            mapOf("demo.plugin" to descriptor(caps = "kernel_read,kernel_hook")),
        ).single()
        assertFalse(row.runUsable)
        /* Still switchable: an unusable plugin must be disable-able/retry-able. */
        assertTrue(row.toggleable)
        val blocked = requireNotNull(row.blockedReason)
        assertEquals(R.string.plugin_issue_field_error, blocked.resId)
        assertTrue(blocked.args.any { arg -> arg.toString().contains("kernel_hook") })
    }

    /**
     * P0 UX regression (the device report: "after switching a plugin off, the
     * switch greys out and it can only be switched on again from another
     * screen"). The disable path rebuilds the rows BEFORE a fresh description
     * exists; that state must still leave the enable switch interactive, and the
     * re-enabled row must become run-usable as soon as the description is back.
     */
    @Test
    fun `disabling a plugin never locks its enable switch`() {
        val disabled = pluginRows(listOf(entry(enabled = false)), emptyMap()).single()
        assertFalse(disabled.enabled)
        assertFalse(disabled.runUsable)
        assertTrue("the enable switch must stay interactive", disabled.toggleable)

        /* Re-enabled with the description back (refreshPlugins re-describes):
         * usable again, without leaving the page. */
        val reenabled = pluginRows(
            listOf(entry(enabled = true)),
            mapOf("demo.plugin" to descriptor()),
        ).single()
        assertTrue(reenabled.enabled)
        assertTrue(reenabled.runUsable)
        assertNull(reenabled.blockedReason)

        /* The device acceptance path: off → on → off → on, four times, always
         * with the row's description-moment (empty map) in between. */
        repeat(4) { cycle ->
            val on = cycle % 2 == 0
            val row = pluginRows(listOf(entry(enabled = on)), emptyMap()).single()
            assertEquals(on, row.enabled)
            assertTrue("cycle " + cycle + " locked the switch", row.toggleable)
        }
    }

    /** Only ENABLED plugins can be selected for a run, whatever the selection says. */
    @Test
    fun `a disabled plugin can never be selected for the run`() {
        val rows = pluginRows(
            listOf(entry(enabled = false)),
            mapOf("demo.plugin" to descriptor()),
            runSelection = setOf("demo.plugin"),
        )
        assertFalse(rows.single().selected)
        /* And it is not counted as a blocking selection either. */
        assertTrue(selectedPluginErrors(rows).isEmpty())
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
        assertEquals("arm_delay_us (uint, REQ) — 200", pluginParamSummary(rows[0], words))
        assertTrue(pluginParamSummary(rows[1], words).startsWith("token (str, REQ)"))
        /* The reason is the validator's, not the page's, so it stays as it is. */
        assertTrue(pluginParamSummary(rows[1], words).contains("required parameter"))
    }

    @Test
    fun `value text round-trips every parameter type`() {
        assertEquals("7", pluginValueText(PluginValue.UInt(7u)))
        assertEquals("-3", pluginValueText(PluginValue.Int(-3L)))
        assertEquals("true", pluginValueText(PluginValue.Bool(true)))
        assertEquals("abc", pluginValueText(PluginValue.Str("abc")))
    }
}
