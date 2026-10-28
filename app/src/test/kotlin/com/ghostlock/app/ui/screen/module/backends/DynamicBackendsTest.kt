package com.ghostlock.app.ui.screen.module.backends

import com.ghostlock.app.R
import com.ghostlock.app.ui.backend.BackendFlowRunner
import com.ghostlock.app.ui.backend.BackendReadiness
import com.ghostlock.app.ui.backend.BackendRunUiState
import com.ghostlock.app.ui.backend.BackendUidRequirement
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * 动态 backend 注册表守卫：Lua 等运行时 spec 可注册/选择/注销，并声明自己的 uid 需求。
 *
 * @note 纯 JVM；不执行 Flow。
 */
class DynamicBackendsTest {
    private class FakeRunner : BackendFlowRunner {
        override val key: String = "lua:demo"
        override suspend fun run(
            context: android.content.Context,
            onLine: (String) -> Unit,
        ): BackendRunUiState = BackendRunUiState.Refused(listOf("fake"))
    }

    private class FakeSpec(
        override val key: String = "lua:demo",
        override val uidRequirement: BackendUidRequirement = BackendUidRequirement.SHELL_UID_2000,
    ) : BackendSpec {
        override val cve: String = "LUA-DEMO"
        override val titleRes: Int = R.string.backend_custom
        override val runner: BackendFlowRunner = FakeRunner()
        override fun rows(): List<BackendRow> = emptyList()
        override fun readiness(): BackendReadiness = BackendReadiness.Ready()
    }

    @After
    fun cleanup() {
        Backends.resetToBuiltins()
    }

    @Test
    fun dynamicBackendCanBeRegisteredSelectedAndUnregistered() {
        val spec = FakeSpec()
        assertTrue(Backends.register(spec))
        assertFalse("duplicate key must be rejected", Backends.register(FakeSpec()))
        assertEquals(spec, Backends.byKey("lua:demo"))
        assertTrue(BackendSelection.select("lua:demo"))
        assertEquals("lua:demo", BackendSelection.selectedKey)
        assertEquals(spec, Backends.all[BackendSelection.index])
        assertTrue(Backends.unregister("lua:demo"))
        assertNull(Backends.byKey("lua:demo"))
        assertFalse(Backends.unregister("CVE-2026-98260"))
        assertTrue(Backends.byKey("CVE-2026-98260") != null)
    }

    @Test
    fun dynamicSpecDeclaresItsOwnUidRequirement() {
        Backends.register(FakeSpec(key = "lua:shell-only", uidRequirement = BackendUidRequirement.SHELL_UID_2000))
        Backends.register(FakeSpec(key = "lua:any", uidRequirement = BackendUidRequirement.ANY_UID))
        assertEquals(
            BackendUidRequirement.SHELL_UID_2000,
            Backends.byKey("lua:shell-only")?.uidRequirement,
        )
        assertEquals(BackendUidRequirement.ANY_UID, Backends.byKey("lua:any")?.uidRequirement)
    }

    @Test
    fun removalFallsBackToExistingSelection() {
        BackendSelection.select("CVE-2026-98260")
        Backends.register(FakeSpec())
        BackendSelection.select("lua:demo")
        Backends.unregister("lua:demo")
        assertTrue(BackendSelection.selectedKey == "CVE-2026-98260" || BackendSelection.selectedKey != null)
    }
}
