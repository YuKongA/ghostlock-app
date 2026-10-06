package com.ghostlock.app.data.plugin

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Batch 1: run-level plugin selection. The default (null) is every enabled
 * plugin, so an untouched App composes byte-identical documents; an EMPTY set is
 * a deliberate "load nothing this run" and must stay distinguishable from the
 * default.
 */
class PluginRunSelectionTest {

    private fun entry(id: String, enabled: Boolean, version: String = "1.0") = PluginManifestEntry(
        id = id,
        version = version,
        abiVersion = 1u,
        sha256 = "a".repeat(64),
        modulePath = id + "/1.0/" + id + ".so",
        enabled = enabled,
        stage = "post_terminal",
        importedAtMs = 7L,
    )

    private val entries = listOf(
        entry("alpha.plugin", enabled = true),
        entry("beta.plugin", enabled = false),
        entry("gamma.plugin", enabled = true, version = "2.0"),
    )

    @Test
    fun `the default loads every enabled plugin and never a disabled one`() {
        assertEquals(
            listOf("alpha.plugin", "gamma.plugin"),
            PluginRunSelection.of(entries, null).map { it.id },
        )
        /* An explicit set that names a disabled plugin does not enable it. */
        assertEquals(
            listOf("alpha.plugin"),
            PluginRunSelection.of(entries, setOf("alpha.plugin", "beta.plugin")).map { it.id },
        )
    }

    @Test
    fun `an empty selection deliberately loads nothing`() {
        assertTrue(PluginRunSelection.of(entries, emptySet()).isEmpty())
        /* Unknown ids simply select nothing. */
        assertTrue(PluginRunSelection.of(entries, setOf("nope")).isEmpty())
    }

    @Test
    fun `counts and the log line describe what the run loads`() {
        assertEquals(2 to 2, PluginRunSelection.counts(entries, null))
        assertEquals(1 to 2, PluginRunSelection.counts(entries, setOf("gamma.plugin")))
        assertEquals(0 to 2, PluginRunSelection.counts(entries, emptySet()))

        assertEquals(
            "plugins: loading alpha.plugin@1.0,gamma.plugin@2.0",
            PluginRunSelection.logLine(entries, null),
        )
        assertEquals(
            "plugins: loading gamma.plugin@2.0; enabled but not selected: alpha.plugin",
            PluginRunSelection.logLine(entries, setOf("gamma.plugin")),
        )
        assertEquals(
            "plugins: loading none; enabled but not selected: alpha.plugin,gamma.plugin",
            PluginRunSelection.logLine(entries, emptySet()),
        )
    }
}
