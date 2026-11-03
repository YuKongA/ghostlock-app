package com.ghostlock.app.backend.declarations

import com.ghostlock.app.BuildConfig
import com.ghostlock.app.R
import com.ghostlock.app.backend.registry.Backends
import com.ghostlock.app.core.flow.FlowSymbol
import com.ghostlock.app.ui.backend.BackendReadiness
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** B4：单一注册表不变量（唯一 key、selectable⇒flowSymbol、debug probe 构建隔离、register fail-closed）。 */
class BackendRegistryInvariantsTest {
    private class FakeSpec(
        override val key: String,
        override val cve: String = "FAKE-CVE",
        override val selectable: Boolean = false,
        override val flowSymbol: FlowSymbol? = null,
    ) : BackendSpec {
        override val titleRes: Int = R.string.backend_custom
        override fun rows(): List<BackendRow> = emptyList()
        override fun readiness(): BackendReadiness = BackendReadiness.Ready()
    }

    @After
    fun cleanup() {
        Backends.resetToBuiltins()
    }

    @Test
    fun builtinsHaveUniqueKeys() {
        val keys = Backends.all.map { it.key }
        assertEquals("duplicate backend key in registry", keys.size, keys.distinct().size)
    }

    @Test
    fun everySelectableSpecExposesFlowEntry() {
        for (spec in Backends.all.filter { it.selectable }) {
            assertNotNull("selectable spec without flowSymbol: " + spec.key, spec.flowSymbol)
        }
    }

    @Test
    fun debugProbePresenceMatchesBuildFlag() {
        val probe = Backends.byKey("DEBUG-PROBE")
        assertEquals("probe must be registered iff BuildConfig.DEBUG", BuildConfig.DEBUG, probe != null)
        if (BuildConfig.DEBUG) {
            assertNotNull(probe)
            assertTrue("probe must be selectable in debug", Backends.isSelectable(probe!!))
            assertNotNull(probe.flowSymbol)
        }
    }

    @Test
    fun isSelectableDelegatesToSelectableFlagOnly() {
        for (spec in Backends.all) {
            assertEquals("isSelectable mismatch: " + spec.key, spec.selectable, Backends.isSelectable(spec))
        }
        val fake = FakeSpec(key = "fake:non-selectable")
        assertTrue(Backends.register(fake))
        assertEquals(fake.selectable, Backends.isSelectable(fake))
    }

    @Test
    fun registerRejectsFailClosedAndKeepsRegistryUnchanged() {
        val before = Backends.all

        assertFalse("blank key must be rejected", Backends.register(FakeSpec(key = "")))
        assertEquals(before, Backends.all)

        assertFalse("duplicate key must be rejected", Backends.register(FakeSpec(key = Builtin.KEY_43499_GLUED)))
        assertEquals(before, Backends.all)

        val selectableWithoutFlow = FakeSpec(key = "fake:selectable-no-flow", selectable = true)
        assertFalse(
            "selectable spec without flowSymbol must be rejected",
            Backends.register(selectableWithoutFlow),
        )
        assertEquals(before, Backends.all)
        assertNull(Backends.byKey("fake:selectable-no-flow"))
    }

    @Test
    fun validNonSelectableDynamicSpecCanRegisterAndUnregister() {
        val fake = FakeSpec(key = "fake:pool")
        assertTrue(Backends.register(fake))
        assertEquals(fake, Backends.byKey("fake:pool"))
        assertTrue(Backends.unregister("fake:pool"))
        assertNull(Backends.byKey("fake:pool"))
        assertFalse("builtin cannot be unregistered", Backends.unregister(Builtin.KEY_43499_GLUED))
    }

    /** 内置 key 常量（避免测试里重复字面量漂移）。 */
    private object Builtin {
        const val KEY_43499_GLUED = "43499-Glued"
    }
}
