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
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** Process-local stable-key selection authority. */
class BackendSessionStoreAuthorityTest {
    /** Restore chooses the first selectable entry and returns null when none exists. */
    @Test
    fun restoreUsesDeterministicSelectableFallback() {
        val store = BackendSessionStore()
        assertEquals("ready-a", store.restore(specs())?.key)
        assertEquals("ready-a", store.selectedKey)
        assertNull(BackendSessionStore().restore(listOf(spec("disabled", false))))
    }

    /** Disabled declarations cannot replace the selected key. */
    @Test
    fun disabledSelectionIsRejected() {
        val store = BackendSessionStore()
        store.restore(specs())
        assertFalse(store.select(spec("disabled", false)))
        assertEquals("ready-a", store.selectedKey)
    }

    /** Selecting a resolved selectable declaration updates exactly this store. */
    @Test
    fun selectableKeyBecomesCurrent() {
        val store = BackendSessionStore()
        val second = specs()[1]
        assertTrue(store.select(second))
        assertEquals("ready-b", store.selectedKey)
    }

    private fun specs(): List<BackendSpec> = listOf(spec("ready-a"), spec("ready-b"), spec("disabled", false))

    private fun spec(key: String, selectable: Boolean = true): BackendSpec = BackendSpec(
        key = key,
        presentation = BackendPresentationIdentity("Test", key),
        selectionPolicy = if (selectable) BackendSelectionPolicy.SELECTABLE else BackendSelectionPolicy.DISABLED,
        abortPolicy = AbortPolicy.SAFE_DURING_RUN,
        executionDomain = ExecutionDomain.APP,
        configuration = BackendConfigurationSpec(),
        parameterRequirements = BackendParameterRequirements.EMPTY,
        runCapability = RunCapability.DIAGNOSTIC_ONLY,
    )
}
