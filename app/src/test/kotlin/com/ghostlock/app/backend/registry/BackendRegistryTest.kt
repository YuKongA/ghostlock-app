package com.ghostlock.app.backend.registry

import com.ghostlock.app.domain.run.RunCapability
import org.junit.Assert.assertEquals
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

    /** Diagnose resolves without a build-type gate and registry membership contains only current keys. */
    @Test
    fun diagnoseMembershipHasNoLegacyAlias() {
        val plugin = requireNotNull(BackendRegistry.plugin("DIAGNOSE"))
        assertEquals(RunCapability.DIAGNOSTIC_ONLY, plugin.spec.runCapability)
        assertEquals(listOf("CUSTOM", "DIAGNOSE"), BackendRegistry.specs.map { spec -> spec.key })
    }
}
