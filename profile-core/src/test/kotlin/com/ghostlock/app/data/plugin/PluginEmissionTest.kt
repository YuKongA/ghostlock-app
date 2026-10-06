package com.ghostlock.app.data.plugin

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test

/** P1 emission: only enabled plugins, registry-pinned identity, declared params. */
class PluginEmissionTest {

    private val sha = "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"

    private fun descriptor(rejected: Boolean = false): PluginDescriptor = PluginProbe.parse(
        "host_abi\t1\n" +
            "countermeasures_root\tcountermeasures\n" +
            "host_stages\tpost_terminal\n" +
            "host_caps\tkernel_read\n" +
            "plugin\tdemo.plugin\t1.0\t1\t64\t" + sha + "\tpost_terminal\tkernel_read\n" +
            "param\tdemo.plugin\tthreshold\tuint\t1\t200\tdoc\n" +
            "param\tdemo.plugin\tmode\tstr\t0\tauto\tdoc\n" +
            "extract\tdemo.plugin\toffset\tuint\t1\t-\tfrom the boot image\n" +
            "extract\tdemo.plugin\tsymbol\tstr\t1\t-\tfrom kallsyms\n" +
            (if (rejected) "reject\tdemo.plugin\treserved capability\n" else ""),
    )

    private fun entry(enabled: Boolean = true, stage: String? = "post_terminal") =
        PluginManifestEntry(
            id = "demo.plugin",
            version = "1.0",
            abiVersion = 1u,
            sha256 = sha,
            modulePath = "demo.plugin/1.0/demo.plugin.so",
            enabled = enabled,
            stage = stage,
            importedAtMs = 7L,
        )

    @Test
    fun `a disabled plugin emits nothing`() {
        assertNull(PluginEmission.of(entry(enabled = false), descriptor()))
    }

    /**
     * P0 (device path): plugins.tsv writes `-` for an unpinned stage. A plugin
     * with ONE declared stage has a defined default policy, and native fails the
     * whole run with StageMissing when the document carries no stage at all.
     */
    @Test
    fun `an unpinned stage falls back to the plugin's only declared stage`() {
        assertEquals("post_terminal", PluginEmission.of(entry(stage = null), descriptor())?.stage)
    }

    /** Several declared stages need a user choice: refuse it visibly, never guess. */
    @Test
    fun `several declared stages cannot be guessed and are refused`() {
        val multi = PluginProbe.parse(
            "host_abi\t1\n" +
                "countermeasures_root\tcountermeasures\n" +
                "host_stages\tpost_terminal,pre_terminal\n" +
                "host_caps\tkernel_read\n" +
                "plugin\tdemo.plugin\t1.0\t1\t64\t" + sha +
                "\tpost_terminal,pre_terminal\tkernel_read\n",
        )
        val error = assertThrows(IllegalArgumentException::class.java) {
            PluginEmission.of(entry(stage = null), multi)
        }
        assertTrue(error.message!!.contains("none is pinned"))
    }

    @Test
    fun `an enabled plugin carries the registry identity and only overridden params`() {
        val emission = requireNotNull(
            PluginEmission.of(
                entry(),
                descriptor(),
                mapOf("threshold" to PluginValue.UInt(9u), "mode" to PluginValue.Str("manual")),
            ),
        )
        assertEquals("demo.plugin", emission.id)
        assertEquals("post_terminal", emission.stage)
        assertEquals("demo.plugin/1.0/demo.plugin.so", emission.modulePath)
        assertEquals(sha, emission.moduleHash)
        assertEquals(listOf("threshold", "mode"), emission.params.map { it.name })
        assertEquals(PluginValue.UInt(9u), emission.params[0].value)
        assertEquals(PluginValue.Str("manual"), emission.params[1].value)
    }

    @Test
    fun `extractor values follow the declared extract rows`() {
        /* Declared keys ride in declaration order, typed by the descriptor. */
        val emission = requireNotNull(
            PluginEmission.of(
                entry(),
                descriptor(),
                extracts = mapOf(
                    "symbol" to PluginValue.Str("task_defex_enforce"),
                    "offset" to PluginValue.UInt(4096u),
                ),
            ),
        )
        assertEquals(
            listOf("offset", "symbol"),
            emission.extract.map { it.name },
        )
        assertEquals(PluginValue.UInt(4096u), emission.extract.first().value)
        assertTrue(emission.params.isEmpty())

        /* An undeclared key is refused outright. */
        val unknown = org.junit.Assert.assertThrows(IllegalArgumentException::class.java) {
            PluginEmission.of(entry(), descriptor(), extracts = mapOf("mystery" to PluginValue.UInt(1u)))
        }
        assertTrue(unknown.message!!.contains("mystery"))

        /* A value whose type differs from the declaration is refused. */
        assertThrows(IllegalArgumentException::class.java) {
            PluginEmission.of(entry(), descriptor(), extracts = mapOf("offset" to PluginValue.Str("4096")))
        }

        /* An unresolved key is simply absent, never defaulted. */
        val unresolved = requireNotNull(
            PluginEmission.of(entry(), descriptor(), extracts = mapOf("symbol" to PluginValue.Str("x"))),
        )
        assertEquals(listOf("symbol"), unresolved.extract.map { it.name })
    }

    @Test
    fun `an override for an undeclared parameter is refused`() {
        val error = assertThrows(IllegalArgumentException::class.java) {
            PluginEmission.of(entry(), descriptor(), mapOf("mystery" to PluginValue.UInt(1u)))
        }
        assertTrue(error.message!!.contains("mystery"))
    }

    @Test
    fun `a value whose type differs from the declaration is refused`() {
        val error = assertThrows(IllegalArgumentException::class.java) {
            PluginEmission.of(entry(), descriptor(), mapOf("threshold" to PluginValue.Str("9")))
        }
        assertTrue(error.message!!.contains("expects uint"))
    }

    @Test
    fun `a stage the plugin does not register and a rejected module are refused`() {
        assertThrows(IllegalArgumentException::class.java) {
            PluginEmission.of(entry(stage = "pre_spawn"), descriptor())
        }
        assertThrows(IllegalArgumentException::class.java) {
            PluginEmission.of(entry(), descriptor(rejected = true))
        }
        assertThrows(IllegalArgumentException::class.java) {
            PluginEmission.of(entry(), descriptor().copy(id = "other.plugin"))
        }
    }
}
