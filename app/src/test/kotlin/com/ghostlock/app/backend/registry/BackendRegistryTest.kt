package com.ghostlock.app.backend.registry

import com.ghostlock.app.BuildConfig
import com.ghostlock.app.domain.run.RunCapability
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotSame
import org.junit.Assert.assertTrue
import org.junit.Test

/** Immutable registry selection and fresh Flow factory contract. */
class BackendRegistryTest {
    /** Membership keys are unique and no product backend is selectable in R3. */
    @Test
    fun membershipIsImmutableAndProductCountIsZero() {
        val keys = BackendRegistry.specs.map { spec -> spec.key }
        assertEquals(keys.toSet().size, keys.size)
        assertEquals(0, BackendRegistry.productionBackendCount)
        assertTrue(
            BackendRegistry.specs.none { spec ->
                spec.selectable && spec.runCapability == RunCapability.PRODUCT_ROOT
            },
        )
    }

    /** DebugProbe resolves by key and creates a new one-shot Flow for every request. */
    @Test
    fun debugProbeFactoryNeverReusesFlow() {
        if (!BuildConfig.DEBUG) return
        val plugin = requireNotNull(BackendRegistry.plugin("DEBUG-PROBE"))
        assertEquals(RunCapability.DIAGNOSTIC_ONLY, plugin.spec.runCapability)
        assertNotSame(plugin.createFlow(), plugin.createFlow())
    }
}
