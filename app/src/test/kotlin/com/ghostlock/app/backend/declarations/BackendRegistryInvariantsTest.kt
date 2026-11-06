package com.ghostlock.app.backend.declarations

import com.ghostlock.app.backend.registry.BackendRegistry
import com.ghostlock.app.domain.run.RunCapability
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** Immutable plugin-registry invariants with no runtime registration surface. */
class BackendRegistryInvariantsTest {
    /** Stable keys are unique and every declaration has a resolved capability. */
    @Test
    fun declarationsHaveUniqueKeysAndResolvedCapabilities() {
        val specs = BackendRegistry.specs
        assertEquals(specs.size, specs.map { spec -> spec.key }.toSet().size)
        assertTrue(specs.none { spec -> spec.runCapability == RunCapability.UNRESOLVED })
    }

    /** Exact lookup never falls through to a prefix or presentation label. */
    @Test
    fun lookupUsesOnlyExactStableKey() {
        BackendRegistry.specs.forEach { spec -> assertEquals(spec, BackendRegistry.spec(spec.key)) }
        assertEquals(null, BackendRegistry.spec("DEBUG"))
        assertEquals(null, BackendRegistry.spec(null))
    }

    /** Dynamic readiness is queried separately from immutable spec data. */
    @Test
    fun availabilityIsSeparateFromSpec() {
        BackendRegistry.specs.forEach { spec ->
            assertNotNull("missing availability for ${spec.key}", BackendRegistry.availability(spec.key))
        }
    }
}
