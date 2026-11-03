package com.ghostlock.app.ui.screen.module.backends

import com.ghostlock.app.BuildConfig
import com.ghostlock.app.R
import com.ghostlock.app.backend.declarations.BackendRow
import com.ghostlock.app.backend.declarations.BackendSelection
import com.ghostlock.app.backend.declarations.BackendSpec
import com.ghostlock.app.backend.registry.Backends
import com.ghostlock.app.backend.backends.cve43499glued.BackendCve43499Glued
import com.ghostlock.app.ui.backend.BackendRunGate
import com.ghostlock.app.ui.backend.BackendReadiness
import com.ghostlock.app.ui.backend.BackendUidRequirement
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * 动态 backend 注册表守卫：Lua 等运行时 spec 可注册/注销，但本工作树保持不可选择。
 *
 * @note 纯 JVM；不执行 Flow。
 */
class DynamicBackendsTest {
    private open class FakeSpec(
        override val key: String = "lua:demo",
        override val uidRequirement: BackendUidRequirement = BackendUidRequirement.SHELL_UID_2000,
        override val selectable: Boolean = false,
    ) : BackendSpec {
        override val cve: String = "LUA-DEMO"
        override val titleRes: Int = R.string.backend_custom
        override fun rows(): List<BackendRow> = emptyList()
        override fun readiness(): BackendReadiness = BackendReadiness.Ready()
    }

    private class ImpersonatingSpec : FakeSpec(key = "lua:impersonating", selectable = true) {
        override fun equals(other: Any?): Boolean = other === BackendCve43499Glued
        override fun hashCode(): Int = System.identityHashCode(BackendCve43499Glued)
    }

    @After
    fun cleanup() {
        Backends.resetToBuiltins()
    }

    @Test
    fun onlyGlueBackendsAndDebugProbeAreSelectableInDebugBuild() {
        val expected = if (BuildConfig.DEBUG) {
            setOf("43499-Glued", "43284Glued", "DEBUG-PROBE")
        } else {
            setOf("43499-Glued", "43284Glued")
        }
        assertEquals(
            expected,
            Backends.all.filter { Backends.isSelectable(it) }.map { it.key }.toSet(),
        )
    }

    @Test
    fun nonGlueBuiltinsAndDynamicLuaRemainVisibleButCannotBeSelected() {
        val expectedDisabled = setOf(
            "CVE-2026-43284",
            "CVE-2026-43499",
            "CUSTOM",
        )
        expectedDisabled.forEach { key ->
            val spec = requireNotNull(Backends.byKey(key))
            assertFalse("$key must remain disabled", Backends.isSelectable(spec))
        }

        /* 新权威：selectable ⇒ 必须自带 flowSymbol；非法 selectable spec 由 register fail-closed 拒绝。 */
        val selectableLua = FakeSpec(key = "lua:selectable", selectable = true)
        assertFalse(
            "selectable spec without flowSymbol must be rejected by registry",
            Backends.register(selectableLua),
        )
        assertNull(Backends.byKey("lua:selectable"))

        val disabledLua = FakeSpec(key = "lua:disabled", selectable = false)
        assertTrue(Backends.register(disabledLua))
        assertFalse("non-selectable Lua cannot be selected", Backends.isSelectable(disabledLua))
        assertFalse("non-selectable Lua cannot enter BackendSelection", BackendSelection.select(disabledLua.key))
    }

    @Test
    fun dynamicSpecCannotImpersonateGlueBackendThroughEquals() {
        val impostor = ImpersonatingSpec()
        /* selectable=true 但无 flowSymbol ⇒ register fail-closed 拒绝，equals 冒充无从进入注册表。 */
        assertFalse("selectable impostor without flowSymbol must be rejected", Backends.register(impostor))
        assertNull(Backends.byKey("lua:impersonating"))
        assertFalse("an equality impostor cannot be selected", BackendSelection.select(impostor.key))
    }

    @Test
    fun dynamicBackendCanBeRegisteredButCannotBeSelected() {
        assertTrue(BackendSelection.select("43499-Glued"))
        val spec = FakeSpec()
        assertTrue(Backends.register(spec))
        assertFalse("duplicate key must be rejected", Backends.register(FakeSpec()))
        assertEquals(spec, Backends.byKey("lua:demo"))
        assertFalse("Lua backend is disabled in the exec-glue build", BackendSelection.select("lua:demo"))
        assertEquals("43499-Glued", BackendSelection.selectedKey)
        assertTrue(Backends.isSelectable(Backends.all[BackendSelection.index]))
        assertTrue(Backends.unregister("lua:demo"))
        assertNull(Backends.byKey("lua:demo"))
    }

    @Test
    fun disabledDynamicBackendIsRejectedByRunGate() {
        val spec = FakeSpec()
        val decision = BackendRunGate.decide(spec, spec.readiness())
        assertFalse("disabled Lua backend must be blocked even when ready", decision.allowed)
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
        BackendSelection.select("43499-Glued")
        Backends.register(FakeSpec())
        assertFalse(BackendSelection.select("lua:demo"))
        Backends.unregister("lua:demo")
        assertEquals("43499-Glued", BackendSelection.selectedKey)
    }
}
