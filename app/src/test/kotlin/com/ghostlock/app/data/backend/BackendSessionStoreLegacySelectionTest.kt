package com.ghostlock.app.data.backend

import com.ghostlock.app.domain.backend.AbortPolicy
import com.ghostlock.app.domain.backend.BackendConfigurationSpec
import com.ghostlock.app.domain.backend.BackendParameterRequirements
import com.ghostlock.app.domain.backend.BackendPresentationIdentity
import com.ghostlock.app.domain.backend.BackendSelectionPolicy
import com.ghostlock.app.domain.backend.BackendSpec
import com.ghostlock.app.domain.backend.ExecutionDomain
import com.ghostlock.app.domain.run.RunCapability
import org.junit.Assert.assertEquals
import org.junit.Test

/** Stable-key behavior after legacy CVE-prefix and Flow-symbol persistence removal. */
class BackendSessionStoreLegacySelectionTest {
    /** Restore preserves an exact selected key across a reordered immutable snapshot. */
    @Test
    fun exactSelectedKeySurvivesRegistryReordering() {
        val first = spec("alpha")
        val second = spec("beta")
        val store = BackendSessionStore()
        store.select(second)
        assertEquals(second, store.restore(listOf(first, second)))
        assertEquals(second, store.restore(listOf(second, first)))
        assertEquals("beta", store.selectedKey)
    }

    /** A removed key falls back to the first selectable current declaration. */
    @Test
    fun missingStableKeyFallsBackWithoutLegacyMatching() {
        val store = BackendSessionStore()
        store.select(spec("removed"))
        assertEquals("current", store.restore(listOf(spec("current")))?.key)
        assertEquals("current", store.selectedKey)
    }

    private fun spec(key: String): BackendSpec = BackendSpec(
        key = key,
        presentation = BackendPresentationIdentity("Test", key),
        selectionPolicy = BackendSelectionPolicy.SELECTABLE,
        abortPolicy = AbortPolicy.SAFE_DURING_RUN,
        executionDomain = ExecutionDomain.APP,
        configuration = BackendConfigurationSpec(),
        parameterRequirements = BackendParameterRequirements.EMPTY,
        runCapability = RunCapability.DIAGNOSTIC_ONLY,
    )
}
