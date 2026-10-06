package com.ghostlock.app.data.plugin

import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * P0 regression: the document build used to THROW when a selected plugin had no
 * cached descriptor — `error("plugin … could not be described; open the plugins
 * page") ` ran inside every profile load, so the process died on every build.
 *
 * The contract these tests pin: describe ON DEMAND, never throw, only SELECTED
 * plugins can block, and a failure carries the probe's own reason.
 */
class PluginSelectionResolverTest {

    private val sha = "a".repeat(64)

    private fun entry(id: String = "glk.probe", enabled: Boolean = true) = PluginManifestEntry(
        id = id,
        version = "1.0",
        abiVersion = 1u,
        sha256 = sha,
        modulePath = id + "/1.0/" + id + ".so",
        enabled = enabled,
        stage = null,
        importedAtMs = 7L,
    )

    private fun descriptor(id: String = "glk.probe") = PluginProbe.parse(
        "host_abi\t1\n" +
            "countermeasures_root\tcountermeasures\n" +
            "host_stages\tpost_terminal\n" +
            "host_caps\tkernel_read\n" +
            "plugin\t" + id + "\t1.0\t1\t64\t" + sha + "\tpost_terminal\tkernel_read\n",
    )

    @Test
    fun `an enabled but unselected plugin is neither described nor blocking`() = runBlocking {
        var calls = 0
        val resolution = PluginSelectionResolver.resolve(
            entries = listOf(entry()),
            runSelection = emptySet(),
            cached = emptyMap(),
        ) {
            calls++
            PluginDescribeResult.Failed("the probe would fail, but this plugin is not selected")
        }
        assertEquals(0, calls)
        assertEquals(PluginSelection.Ready(emptyList()), resolution.selection)
    }

    @Test
    fun `a selected plugin without a cached descriptor is described on demand`() = runBlocking {
        var calls = 0
        val resolution = PluginSelectionResolver.resolve(
            entries = listOf(entry()),
            runSelection = null,
            cached = emptyMap(),
        ) {
            calls++
            PluginDescribeResult.Described(descriptor())
        }
        assertEquals(1, calls)
        val ready = resolution.selection as PluginSelection.Ready
        assertEquals(listOf("glk.probe"), ready.plugins.map { it.entry.id })
        assertEquals(setOf("glk.probe"), resolution.descriptors.keys)
    }

    @Test
    fun `a cached descriptor is reused instead of re-probed`() = runBlocking {
        var calls = 0
        val resolution = PluginSelectionResolver.resolve(
            entries = listOf(entry()),
            runSelection = null,
            cached = mapOf("glk.probe" to descriptor()),
        ) {
            calls++
            PluginDescribeResult.Failed("must not run")
        }
        assertEquals(0, calls)
        assertTrue(resolution.selection is PluginSelection.Ready)
    }

    @Test
    fun `a failed description blocks with the probe's own reason, never an exception`() = runBlocking {
        val resolution = PluginSelectionResolver.resolve(
            entries = listOf(entry()),
            runSelection = null,
            cached = emptyMap(),
        ) {
            PluginDescribeResult.Failed("the module file is missing: /data/x/glk.probe.so")
        }
        val blocked = resolution.selection as PluginSelection.Blocked
        assertEquals(
            listOf("glk.probe: the module file is missing: /data/x/glk.probe.so"),
            blocked.reasons,
        )
    }

    @Test
    fun `a broken plugin that this run does not select cannot block a good one`() = runBlocking {
        val resolution = PluginSelectionResolver.resolve(
            entries = listOf(entry("bad.plugin"), entry("glk.probe")),
            runSelection = setOf("glk.probe"),
            cached = emptyMap(),
        ) { candidate ->
            if (candidate.id == "bad.plugin") {
                PluginDescribeResult.Failed("cannot run the native probe")
            } else {
                PluginDescribeResult.Described(descriptor(candidate.id))
            }
        }
        assertTrue(resolution.selection is PluginSelection.Ready)
    }
}
