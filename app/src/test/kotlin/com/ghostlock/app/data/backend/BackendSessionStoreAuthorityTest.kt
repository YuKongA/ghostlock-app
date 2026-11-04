package com.ghostlock.app.data.backend

import com.ghostlock.app.R
import com.ghostlock.app.backend.declarations.BackendReadiness
import com.ghostlock.app.backend.declarations.BackendRow
import com.ghostlock.app.backend.declarations.BackendSpec
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** Verifies the process-local selection state owned by one backend session store. */
class BackendSessionStoreAuthorityTest {
    /** A missing persisted value selects the first selectable captured backend in memory. */
    @Test
    fun missingPersistedSelectionUsesCapturedFallback() {
        val selection = BackendSessionSelection()
        assertNull(selection.restore(specs(), null))
        assertEquals("ready-a", selection.selectedKey)
    }

    /** A valid persisted value becomes the exact current stable key. */
    @Test
    fun persistedSelectionRestoresExactCapturedBackend() {
        val entries = specs()
        val selection = BackendSessionSelection()
        assertEquals(entries[1], selection.restore(entries, "ready-b"))
        assertEquals("ready-b", selection.selectedKey)
    }

    /** Explicit selection changes only the same injected authority instance. */
    @Test
    fun explicitSelectionUpdatesSessionAuthority() {
        val entries = specs()
        val selection = BackendSessionSelection()
        selection.restore(entries, null)
        assertTrue(selection.select(entries[1]))
        assertEquals("ready-b", selection.selectedKey)
    }

    /** A disabled backend cannot replace the current application-scoped selection. */
    @Test
    fun disabledBackendDoesNotMutateSelection() {
        val entries = specs()
        val selection = BackendSessionSelection()
        selection.restore(entries, null)
        assertFalse(selection.select(entries[2]))
        assertEquals("ready-a", selection.selectedKey)
    }

    /** @return Two selectable fixtures and one disabled fixture in registry order. */
    private fun specs(): List<BackendSpec> = listOf(
        FakeSpec("ready-a", true),
        FakeSpec("ready-b", true),
        FakeSpec("disabled", false),
    )

    /** Minimal backend contract used to isolate session selection behavior. */
    private class FakeSpec(
        override val key: String,
        override val selectable: Boolean,
    ) : BackendSpec {
        override val cve: String = key
        override val titleRes: Int = R.string.backend_custom
        override fun rows(): List<BackendRow> = emptyList()
        override fun readiness(): BackendReadiness = BackendReadiness.Ready()
    }

}
