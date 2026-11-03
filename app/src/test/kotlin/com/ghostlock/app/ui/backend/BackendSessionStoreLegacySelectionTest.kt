package com.ghostlock.app.ui.backend

import com.ghostlock.app.R
import com.ghostlock.app.backend.declarations.BackendRow
import com.ghostlock.app.backend.declarations.BackendSpec
import com.ghostlock.app.core.flow.FlowSymbol
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

/** B4：SessionStore 选择解析纯函数（exact key 优先；legacy cve 前缀仅迁移、不构成第二权威）。 */
class BackendSessionStoreLegacySelectionTest {
    private class FakeSpec(
        override val key: String,
        override val cve: String,
        override val selectable: Boolean = true,
        override val flowSymbol: FlowSymbol? =
            FlowSymbol("com.ghostlock.app.backend.backends.debugprobe.FlowDebugProbeEntry", "entry"),
    ) : BackendSpec {
        override val titleRes: Int = R.string.backend_custom
        override fun rows(): List<BackendRow> = emptyList()
        override fun readiness(): BackendReadiness = BackendReadiness.Ready()
    }

    @Test
    fun exactKeyWinsOverLegacyPrefix() {
        val exact = FakeSpec(key = "43499-Glued", cve = "CVE-2026-43499-Glued")
        val legacy = FakeSpec(key = "other", cve = "43499-Glued-legacy-extra")
        assertEquals(exact, resolveStoredSpec(listOf(legacy, exact), "43499-Glued"))
    }

    @Test
    fun exactCveValueResolves() {
        val spec = FakeSpec(key = "43499-Glued", cve = "CVE-2026-43499-Glued")
        assertEquals(spec, resolveStoredSpec(listOf(spec), "CVE-2026-43499-Glued"))
    }

    @Test
    fun legacyCvePrefixResolvesWhenUnique() {
        val spec = FakeSpec(key = "43499-Glued", cve = "CVE-2026-43499-Glued")
        assertEquals(spec, resolveStoredSpec(listOf(spec), "CVE-2026-43499"))
    }

    @Test
    fun ambiguousLegacyPrefixReturnsNull() {
        val first = FakeSpec(key = "a", cve = "CVE-2026-43499-a")
        val second = FakeSpec(key = "b", cve = "CVE-2026-43499-b")
        assertNull(resolveStoredSpec(listOf(first, second), "CVE-2026-43499"))
    }

    @Test
    fun noMatchReturnsNull() {
        assertNull(resolveStoredSpec(emptyList(), "CVE-2026-00000"))
        assertNull(
            resolveStoredSpec(
                listOf(FakeSpec(key = "43499-Glued", cve = "CVE-2026-43499-Glued")),
                "CVE-2026-99999",
            ),
        )
    }

    @Test
    fun exactNonSelectableDoesNotFallThroughToLegacy() {
        val exact = FakeSpec(key = "43499-Glued", cve = "CVE-2026-43499-Glued", selectable = false)
        val legacy = FakeSpec(key = "legacy", cve = "43499-Glued-extra")
        assertNull(resolveStoredSpec(listOf(exact, legacy), "43499-Glued"))
    }

    @Test
    fun nonSelectableLegacyCandidatesAreIgnored() {
        val spec = FakeSpec(key = "a", cve = "CVE-2026-43499-a", selectable = false)
        assertNull(resolveStoredSpec(listOf(spec), "CVE-2026-43499"))
    }
}
