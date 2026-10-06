package com.ghostlock.app.data.plugin

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The Kotlin parser against a REAL device capture of `glk.probe` — a SECOND
 * input next to the synthetic `plugin-probe-golden.tsv`, never a replacement
 * for it: that fixture's identity rows are synthetic (`test.schema`) and a
 * re-capture cannot reproduce them.
 *
 * Provenance: `build/gate-logs/plugin-probe-golden-device.tsv`
 * (sha256 1a6e49d8c015e7d036730f1cbfc66d89d12b39348c04b0d0c4fbbeb9293d35a9),
 * byte-identical copy in the test resources. Its point for batch B: the probe
 * now reports the `log` capability (`host_caps`), and the App must parse it —
 * capabilities are opaque tokens here, so a new one needs no Kotlin change.
 */
class PluginProbeDeviceCaptureTest {

    private fun capture(): String = checkNotNull(
        javaClass.classLoader?.getResourceAsStream("plugin-probe-glk-probe-device.tsv"),
    ) { "missing plugin-probe-glk-probe-device.tsv" }.bufferedReader().use { it.readText() }

    @Test
    fun `the real glk probe capture parses, log capability included`() {
        val descriptor = PluginProbe.parse(capture())
        assertEquals("glk.probe", descriptor.id)
        assertEquals("1.0", descriptor.version)
        assertEquals(1u, descriptor.abiVersion)
        assertEquals(80u, descriptor.size)
        assertEquals(
            "b7e4891d89634c6a3681eb57c236e803b195e5f95ba774b9d3ba10a8f23a6253",
            descriptor.sha256,
        )
        assertEquals(setOf("post_terminal"), descriptor.stages)
        assertEquals(setOf("kernel_read", "kernel_write"), descriptor.requiredCaps)
        /* The batch B increment, on a capture the DEVICE produced. */
        assertEquals(
            setOf("kernel_read", "kernel_write", "alias", "child_task", "log"),
            descriptor.hostCaps,
        )
        assertEquals(
            setOf("pre_spawn", "post_spawn", "pre_terminal", "post_terminal"),
            descriptor.hostStages,
        )
        assertEquals(
            mapOf("43499" to listOf("pre_terminal"), "43284" to listOf("post_terminal")),
            descriptor.stageAvailability,
        )
        assertEquals(1u, descriptor.hostAbiVersion)
        assertEquals("countermeasures", descriptor.countermeasuresRoot)
        assertEquals(
            PluginPaths.ProbeRootCheck.Match,
            PluginPaths.checkProbeRoot(descriptor.countermeasuresRoot),
        )
        assertTrue(descriptor.usable)
        assertTrue(descriptor.rejects.isEmpty())
    }

    @Test
    fun `the capture hook, parameters and extract row are read as declared`() {
        val descriptor = PluginProbe.parse(capture())
        val hook = descriptor.hooks.single()
        assertEquals("on_stage", hook.trigger)
        assertEquals("post_terminal", hook.stage)
        assertEquals(0u, hook.priority)
        assertEquals("glk.probe", hook.name)

        val params = descriptor.params
        assertEquals(listOf("target_va", "label"), params.map { it.name })
        assertEquals(PluginParamType.UInt, params[0].type)
        assertTrue(params[0].required)
        assertEquals(PluginValue.UInt(18446743524671239168uL), params[0].defaultValue)
        assertEquals(PluginParamType.Str, params[1].type)
        assertTrue(!params[1].required)
        assertEquals("diagnostic label for the glk.probe log line", params[1].doc)

        val extract = descriptor.extract.single()
        assertEquals("platform.abi.offset.init_task", extract.name)
        assertEquals(PluginParamType.UInt, extract.type)
        assertTrue(extract.required)
        assertEquals(PluginValue.UInt(0u), extract.defaultValue)
        assertEquals(
            "init_task image offset (base-relative), from the extractor profile path",
            extract.doc,
        )
        /* An extract key is not a parameter. */
        assertNull(descriptor.paramsByName["platform.abi.offset.init_task"])
        assertNull(descriptor.extractByName["target_va"])
    }
}
