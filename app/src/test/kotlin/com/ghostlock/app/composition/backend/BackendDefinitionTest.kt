package com.ghostlock.app.composition.backend

import com.ghostlock.app.domain.run.RunCapability
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** Immutable outer-definition registry invariants with no runtime registration surface. */
class BackendDefinitionTest {
    /** Stable keys are unique and both projections preserve the sole definition order. */
    @Test
    fun builtInDefinitionsProjectOneMembershipSnapshot() {
        val descriptors = BackendRegistry.descriptors
        val runtimeKeys = descriptors.map { descriptor ->
            requireNotNull(BackendRegistry.runtime(descriptor.key)).descriptor.key
        }
        val uiKeys = BackendRegistry.uiEntries.map { entry -> entry.descriptor.key }
        assertEquals(listOf("CUSTOM", "DIAGNOSE"), descriptors.map { descriptor -> descriptor.key })
        assertEquals(descriptors.size, descriptors.map { descriptor -> descriptor.key }.toSet().size)
        assertEquals(descriptors.map { descriptor -> descriptor.key }, runtimeKeys)
        assertEquals(descriptors.map { descriptor -> descriptor.key }, uiKeys)
        assertTrue(descriptors.none { descriptor -> descriptor.runCapability == RunCapability.UNRESOLVED })
    }

    /** Exact lookup never falls through to a prefix, alias, or presentation label. */
    @Test
    fun runtimeLookupUsesOnlyExactStableKey() {
        assertNotNull(BackendRegistry.runtime("DIAGNOSE"))
        assertNull(BackendRegistry.runtime("DEBUG"))
        assertNull(BackendRegistry.descriptor(null))
    }

    /** A duplicate key fails before a second runtime or UI membership projection can exist. */
    @Test
    fun duplicateDefinitionKeyFailsClosed() {
        val duplicate = DiagnoseBackendDefinition.definition
        val failure = runCatching {
            BackendDefinitionRegistry(listOf(duplicate, duplicate))
        }.exceptionOrNull()
        assertTrue(failure is IllegalArgumentException)
    }

    /** Disconnected real attacks permit zero selectable product-root definitions. */
    @Test
    fun productionBackendCountIsZero() {
        assertEquals(0, BackendRegistry.productionBackendCount)
    }
}
