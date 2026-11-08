package com.ghostlock.app.data.backend

import com.ghostlock.app.domain.backend.AbortPolicy
import com.ghostlock.app.domain.backend.BackendSelectionPolicy
import com.ghostlock.app.domain.backend.BackendDescriptor
import com.ghostlock.app.domain.backend.ExecutionDomain
import com.ghostlock.app.domain.run.RunCapability
import com.ghostlock.app.domain.settings.AppSelection
import com.ghostlock.app.domain.settings.AppSelectionPort
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
    /** Restore chooses the first selectable entry and returns null when none exists. */
    @Test
    fun restoreUsesDeterministicSelectableFallback() = runTest {
        val store = BackendSessionStore(FakeSelectionPort())
        assertEquals("ready-a", store.restore(specs())?.key)
        assertEquals("ready-a", store.selectedKey)
        assertNull(BackendSessionStore(FakeSelectionPort()).restore(listOf(spec("disabled", false))))
    }

    /** Disabled declarations cannot replace the selected key. */
    @Test
    fun disabledSelectionIsRejected() = runTest {
        val store = BackendSessionStore(FakeSelectionPort())
        store.restore(specs())
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

    private fun specs(): List<BackendDescriptor> = listOf(spec("ready-a"), spec("ready-b"), spec("disabled", false))

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
