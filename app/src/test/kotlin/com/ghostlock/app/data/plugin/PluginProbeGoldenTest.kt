package com.ghostlock.app.data.plugin

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Hard assertions against the DEVICE-CAPTURED probe stdout
 * (`app/src/test/resources/plugin-probe-golden.tsv`, commit 7d54ce78): four
 * header rows plus one plugin/hook/four params/one extract row, byte-for-byte
 * from `GHOSTLOCK_HOME=/data/local/tmp ./glk --plugin-probe …` on the gate
 * device (source and batch in docs/analysis/device-gates/s4-p1-probe-20261005-pass.md).
 *
 * This is the Kotlin half of the probe-format agreement: a format change on
 * either side fails here instead of at import time on a device.
 */
class PluginProbeGoldenTest {

    private fun golden(): String = checkNotNull(
        javaClass.classLoader?.getResourceAsStream("plugin-probe-golden.tsv"),
    ) { "missing plugin-probe-golden.tsv" }.bufferedReader().use { it.readText() }

    @Test
    fun `the device capture parses into the documented descriptor`() {
        val descriptor = PluginProbe.parse(golden())
        assertEquals("test.schema", descriptor.id)
        assertEquals("1.2.3", descriptor.version)
        assertEquals(1u, descriptor.abiVersion)
        assertEquals(80u, descriptor.size)
        assertEquals(
            "decc767346129b6dea4a8fb8d907daa13c48d9a44fd60645d5e57e42614205cf",
            descriptor.sha256,
        )
        assertEquals(setOf("pre_spawn", "post_terminal"), descriptor.stages)
        assertEquals(setOf("kernel_read", "alias"), descriptor.requiredCaps)
        assertEquals(1u, descriptor.hostAbiVersion)
        assertEquals(setOf("pre_spawn", "post_spawn", "pre_terminal", "post_terminal"), descriptor.hostStages)
        assertEquals(setOf("kernel_read", "kernel_write", "alias", "child_task"), descriptor.hostCaps)
        assertTrue(descriptor.usable)
        assertTrue(descriptor.rejects.isEmpty())
        assertEquals(1, descriptor.hooks.size)
        assertEquals("on_stage", descriptor.hooks.single().trigger)
        assertEquals("post_terminal", descriptor.hooks.single().stage)
        assertEquals(10u, descriptor.hooks.single().priority)
        assertEquals("schema-hook", descriptor.hooks.single().name)
    }

    @Test
    fun `the header pins the relative countermeasures root`() {
        val descriptor = PluginProbe.parse(golden())
        assertEquals("countermeasures", descriptor.countermeasuresRoot)
        assertEquals(
            PluginPaths.ProbeRootCheck.Match,
            PluginPaths.checkProbeRoot(descriptor.countermeasuresRoot),
        )
    }

    @Test
    fun `all four parameter types, required flags and the empty doc parse`() {
        val params = PluginProbe.parse(golden()).params
        assertEquals(listOf("threshold", "mode", "enabled", "delta"), params.map { it.name })
        assertEquals(PluginParamType.UInt, params[0].type)
        assertTrue(params[0].required)
        assertEquals(PluginValue.UInt(200u), params[0].defaultValue)
        assertEquals("uint parameter", params[0].doc)
        assertEquals(PluginParamType.Str, params[1].type)
        assertEquals(PluginValue.Str("auto"), params[1].defaultValue)
        assertEquals(PluginParamType.Bool, params[2].type)
        assertEquals(PluginValue.Bool(true), params[2].defaultValue)
        assertEquals(PluginParamType.Int, params[3].type)
        assertEquals(PluginValue.Int(0), params[3].defaultValue)
        /* The empty doc column is "-" in the capture: it must not leak in. */
        assertEquals("", params[3].doc)
        val extract = PluginProbe.parse(golden()).extract.single()
        assertEquals("task_offset", extract.name)
        assertTrue(extract.required)
        assertEquals(PluginValue.UInt(0u), extract.defaultValue)
        assertEquals("extractor-provided offset", extract.doc)
        assertNull(PluginProbe.parse(golden()).paramsByName["task_offset"])
    }

    @Test
    fun `a key or a type outside the descriptor is rejected`() {
        val descriptor = PluginProbe.parse(golden())
        val unknown = PluginConfigValidator.validate(
            descriptor,
            enabled = true,
            stage = "post_terminal",
            overrides = mapOf("mystery" to PluginValue.UInt(1u)),
        )
        assertEquals(listOf("plugin.test.schema.params.mystery"), unknown.map { it.path })

        val wrongType = PluginConfigValidator.validate(
            descriptor,
            enabled = true,
            stage = "post_terminal",
            overrides = mapOf("threshold" to PluginValue.Str("200")),
        )
        assertEquals(listOf("plugin.test.schema.params.threshold"), wrongType.map { it.path })
        assertTrue(wrongType.single().reason.contains("expected uint"))
    }
}
