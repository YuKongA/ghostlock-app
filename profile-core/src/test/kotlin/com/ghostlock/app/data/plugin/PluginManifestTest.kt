package com.ghostlock.app.data.plugin

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test

/** P1 registry TSV codec: round-trip plus fail-closed malformed input. */
class PluginManifestTest {

    private val sha = "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"

    private val entry = PluginManifestEntry(
        id = "vivo.vr_guard",
        version = "1.2.0",
        abiVersion = 1u,
        sha256 = sha,
        modulePath = "vivo.vr_guard/1.2.0/vivo_vr_guard.so",
        enabled = true,
        stage = "post_terminal",
        importedAtMs = 1_700_000_000_000L,
    )

    @Test
    fun `render then parse round-trips`() {
        val parsed = PluginManifest.parse(PluginManifest.render(listOf(entry)))
        assertEquals(listOf(entry), parsed)
    }

    @Test
    fun `an absent stage is rendered as a dash and parses back to null`() {
        val text = PluginManifest.render(listOf(entry.copy(stage = null)))
        assertTrue(text.contains("\t-\t"))
        assertNull(PluginManifest.parse(text).single().stage)
    }

    @Test
    fun `rows are rendered in id order`() {
        val text = PluginManifest.render(
            listOf(entry.copy(id = "zzz.plugin"), entry.copy(id = "aaa.plugin")),
        )
        val ids = PluginManifest.parse(text).map { it.id }
        assertEquals(listOf("aaa.plugin", "zzz.plugin"), ids)
    }

    @Test
    fun `module paths are relative to the countermeasures root`() {
        assertEquals(
            "vivo.vr_guard/1.2.0/vivo_vr_guard.so",
            PluginPaths.modulePath("vivo.vr_guard", "1.2.0", "vivo_vr_guard.so"),
        )
        assertTrue(PluginPaths.isSafeModulePath(entry.modulePath))
    }

    @Test
    fun `an unsafe module path is rejected`() {
        for (path in listOf(
            "/vivo.vr_guard/1.2.0/a.so",
            "../../etc/passwd",
            "countermeasures/vivo.vr_guard/1.2.0/a.so",
            "vivo.vr_guard/../a.so",
        )) {
            val text = PluginManifest.render(listOf(entry.copy(modulePath = path)))
            val error = assertThrows(IllegalArgumentException::class.java) {
                PluginManifest.parse(text)
            }
            assertTrue(error.message!!.contains("unsafe module path"))
        }
    }

    @Test
    fun `a duplicate id is rejected`() {
        val row = PluginManifest.render(listOf(entry)).lineSequence()
            .filter { !it.startsWith("#") && it.isNotBlank() }.joinToString("\n")
        val error = assertThrows(IllegalArgumentException::class.java) {
            PluginManifest.parse(row + "\n" + row + "\n")
        }
        assertTrue(error.message!!.contains("duplicate plugin id"))
    }

    @Test
    fun `a malformed sha and column count are rejected`() {
        val badSha = PluginManifest.render(listOf(entry)).replace(sha, "not-a-hash")
        assertThrows(IllegalArgumentException::class.java) { PluginManifest.parse(badSha) }
        assertThrows(IllegalArgumentException::class.java) { PluginManifest.parse("a\tb\n") }
    }
}
