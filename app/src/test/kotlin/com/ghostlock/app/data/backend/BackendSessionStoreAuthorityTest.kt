package com.ghostlock.app.data.backend

import com.ghostlock.app.domain.backend.AbortPolicy
import com.ghostlock.app.domain.backend.BackendSelectionPolicy
import com.ghostlock.app.domain.backend.BackendDescriptor
import com.ghostlock.app.domain.backend.ExecutionDomain
import com.ghostlock.app.domain.run.RunCapability
import com.ghostlock.app.domain.settings.AppSelection
import com.ghostlock.app.domain.settings.AppSelectionPort
import com.ghostlock.app.domain.settings.BackendSelectionState
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.Deferred
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** Process-local stable-key selection authority. */
class BackendSessionStoreAuthorityTest {
    /** A fresh install uses only the authenticated explicit default, never list order. */
    @Test
    fun restoreUsesOnlyExplicitDefaultForFreshSelection() = runTest {
        val port = FakeSelectionPort()
        val store = BackendSessionStore(port)
        assertEquals(
            "ready-b",
            store.restore(specs(), "ready-b", compatibleBackendKeys = readyKeys())?.key,
        )
        assertEquals("ready-b", store.selectedKey)
        assertEquals(BackendSelectionState.SELECTED, port.selection.value.backendSelectionState)
        assertEquals("ready-b", port.selection.value.selectedBackendKey)

        val absentPort = FakeSelectionPort()
        val absent = BackendSessionStore(absentPort)
        assertNull(absent.restore(specs(), "missing", compatibleBackendKeys = readyKeys()))
        assertNull(absent.selectedKey)
        assertEquals(BackendSelectionState.NEEDS_USER_CHOICE, absentPort.selection.value.backendSelectionState)
    }

    /** Disabled declarations cannot replace the selected key. */
    @Test
    fun disabledSelectionIsRejected() = runTest {
        val store = BackendSessionStore(FakeSelectionPort())
        store.restore(specs(), "ready-a", compatibleBackendKeys = readyKeys())
        assertFalse(store.select(spec("disabled", false)))
        assertEquals("ready-a", store.selectedKey)
    }

    /** Selecting a resolved selectable declaration updates exactly this store. */
    @Test
    fun selectableKeyBecomesCurrent() = runTest {
        val store = BackendSessionStore(FakeSelectionPort())
        val second = specs()[1]
        assertTrue(store.select(second))
        assertEquals("ready-b", store.selectedKey)
    }

    @Test
    fun incompatibleExplicitDefaultRequiresUserChoiceWithoutFallback() = runTest {
        val port = FakeSelectionPort()
        val store = BackendSessionStore(port)

        assertNull(
            store.restore(
                specs(),
                defaultBackendKey = "ready-a",
                compatibleBackendKeys = setOf("ready-b"),
            ),
        )
        assertEquals(BackendSelectionState.NEEDS_USER_CHOICE, port.selection.value.backendSelectionState)
        assertNull(store.selectedKey)
    }

    private fun specs(): List<BackendDescriptor> = listOf(spec("ready-a"), spec("ready-b"), spec("disabled", false))

    private fun readyKeys(): Set<String> = setOf("ready-a", "ready-b")

    private fun spec(key: String, selectable: Boolean = true): BackendDescriptor = BackendDescriptor(
        key = key,
        selectionPolicy = if (selectable) BackendSelectionPolicy.SELECTABLE else BackendSelectionPolicy.DISABLED,
        abortPolicy = AbortPolicy.SAFE_DURING_RUN,
        executionDomain = ExecutionDomain.APP,
        runCapability = RunCapability.DIAGNOSTIC_ONLY,
    )

    /** Minimal typed selection authority used by the pure session-store tests. */
    private class FakeSelectionPort : AppSelectionPort {
        private val mutable = MutableStateFlow(AppSelection())
        override val selection: StateFlow<AppSelection> = mutable
        override fun submit(transform: (AppSelection) -> AppSelection): Deferred<AppSelection> {
            val value = transform(mutable.value)
            mutable.value = value
            return CompletableDeferred(value)
        }
        override suspend fun awaitInitial(): AppSelection = mutable.value
        override fun snapshot(): Deferred<AppSelection> = CompletableDeferred(mutable.value)
    }
}
