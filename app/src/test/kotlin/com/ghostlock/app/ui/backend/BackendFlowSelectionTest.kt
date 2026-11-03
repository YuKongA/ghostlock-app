package com.ghostlock.app.ui.backend

import com.ghostlock.app.BuildConfig
import com.ghostlock.app.backend.backends.cve43284glued.BackendCve43284Glued
import com.ghostlock.app.backend.backends.debugprobe.BackendDebugProbe
import com.ghostlock.app.backend.backends.cve43499glued.BackendCve43499Glued
import com.ghostlock.app.backend.registry.Backends
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** B4a（I5）：迁移期单权威选择——43499 走 flowSymbol，其它 backend 保持旧 runner。 */
class BackendFlowSelectionTest {
    @Test
    fun `43499 glued uses flowSymbol only after in-process runner removal`() {
        assertNotNull(BackendCve43499Glued.flowSymbol)
        assertEquals(
            "com.ghostlock.app.backend.backends.cve43499glued.Flow43499GluedEntry#entry",
            BackendCve43499Glued.flowSymbol?.canonical,
        )
        assertEquals(BackendRunCapability.FLOW_RUN, BackendCve43499Glued.runCapability)
    }

    @Test
    fun `43284 glued uses flowSymbol only after in-process runner removal`() {
        assertNotNull(BackendCve43284Glued.flowSymbol)
        assertEquals(
            "com.ghostlock.app.backend.backends.cve43284glued.Flow43284GluedEntry#entry",
            BackendCve43284Glued.flowSymbol?.canonical,
        )
        assertEquals(BackendRunCapability.FLOW_RUN, BackendCve43284Glued.runCapability)
    }

    @Test
    fun `debug probe is only exposed in debug builds`() {
        if (BuildConfig.DEBUG) {
            assertNotNull(BackendDebugProbe.flowSymbol)
            assertEquals(
                "com.ghostlock.app.backend.backends.debugprobe.FlowDebugProbeEntry#entry",
                BackendDebugProbe.flowSymbol?.canonical,
            )
            assertEquals(BackendRunCapability.FLOW_RUN, BackendDebugProbe.runCapability)
            assertTrue(Backends.isSelectable(BackendDebugProbe))
        } else {
            assertNull(BackendDebugProbe.flowSymbol)
            assertEquals(BackendRunCapability.NO_FLOW_RUN, BackendDebugProbe.runCapability)
            assertTrue(!Backends.isSelectable(BackendDebugProbe))
        }
    }

    @Test
    fun `runCapability derives from flowSymbol only`() {
        val migrated = Backends.all.filter { it.flowSymbol != null }
        assertTrue(
            "both Glued backends must be migrated",
            migrated.map { it.key }.containsAll(listOf("43499-Glued", "43284Glued")),
        )
        for (spec in Backends.all) {
            assertEquals(
                "runCapability must derive from flowSymbol (" + spec.key + ")",
                spec.flowSymbol != null,
                spec.runCapability == BackendRunCapability.FLOW_RUN,
            )
        }
    }
}
