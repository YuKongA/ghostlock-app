package com.ghostlock.app.data.plugin

import java.io.File
import java.nio.file.Files
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test

/** P1 registry file behaviour: atomic writes, no temp leftovers, fail closed. */
class PluginStoreTest {

    private fun root(): File = Files.createTempDirectory("glk-plugin-store").toFile()

    private fun entry(id: String = "demo.plugin", enabled: Boolean = false) = PluginManifestEntry(
        id = id,
        version = "1.0",
        abiVersion = 1,
        sha256 = "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd",
        modulePath = id + "/1.0/" + id + ".so",
        enabled = enabled,
        stage = "post_terminal",
        importedAtMs = 42L,
    )

    @Test
    fun `a missing registry loads as empty`() {
        assertEquals(emptyList<PluginManifestEntry>(), PluginStore(root()).load())
    }

    @Test
    fun `upsert writes, replaces and reloads`() {
        val store = PluginStore(root())
        store.upsert(entry("a.plugin"))
        store.upsert(entry("b.plugin"))
        store.upsert(entry("a.plugin", enabled = true))
        val loaded = store.load()
        assertEquals(listOf("a.plugin", "b.plugin"), loaded.map { it.id })
        assertTrue(loaded.first { it.id == "a.plugin" }.enabled)
        assertFalse(File(store.manifestFile.parentFile, store.manifestFile.name + ".tmp").exists())
    }

    @Test
    fun `setEnabled and remove operate on the registry`() {
        val store = PluginStore(root())
        store.upsert(entry("a.plugin"))
        assertEquals(true, store.setEnabled("a.plugin", true).single().enabled)
        assertEquals(emptyList<PluginManifestEntry>(), store.remove("a.plugin"))
        assertEquals(emptyList<PluginManifestEntry>(), store.load())
        assertThrows(IllegalArgumentException::class.java) { store.setEnabled("missing", true) }
    }

    @Test
    fun `a corrupted registry fails closed`() {
        val store = PluginStore(root())
        store.manifestFile.parentFile?.mkdirs()
        store.manifestFile.writeText("garbage\n")
        assertThrows(IllegalArgumentException::class.java) { store.load() }
    }

    @Test
    fun `save creates the root when it is missing`() {
        val base = root()
        val nested = File(base, "no-backup/countermeasures")
        val store = PluginStore(nested)
        store.upsert(entry())
        assertTrue(nested.isDirectory)
        assertEquals(1, store.load().size)
    }
}
