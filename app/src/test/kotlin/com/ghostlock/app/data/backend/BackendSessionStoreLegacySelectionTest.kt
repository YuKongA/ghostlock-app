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
import org.junit.Test

/** Stable-key behavior after legacy CVE-prefix and Flow-symbol persistence removal. */
class BackendSessionStoreLegacySelectionTest {
    /** Restore preserves an exact selected key across a reordered immutable snapshot. */
    @Test
    fun exactSelectedKeySurvivesRegistryReordering() = runTest {
        val first = spec("alpha")
        val second = spec("beta")
        val store = BackendSessionStore(FakeSelectionPort())
        store.select(second)
        assertEquals(second, store.restore(listOf(first, second), "alpha", setOf("alpha", "beta")))
        assertEquals(second, store.restore(listOf(second, first), "alpha", setOf("alpha", "beta")))
        assertEquals("beta", store.selectedKey)
    }

    /** A removed key requires an explicit new choice and never falls back by list order. */
    @Test
    fun missingStableKeyRequiresUserChoiceWithoutLegacyMatching() = runTest {
        val port = FakeSelectionPort()
        val store = BackendSessionStore(port)
        store.select(spec("removed"))
        assertEquals(null, store.restore(listOf(spec("current")), "current", setOf("current")))
        assertEquals(null, store.selectedKey)
        assertEquals(BackendSelectionState.NEEDS_USER_CHOICE, port.selection.value.backendSelectionState)
        assertEquals(null, port.selection.value.selectedBackendKey)
    }

    private fun spec(key: String): BackendDescriptor = BackendDescriptor(
        key = key,
        selectionPolicy = BackendSelectionPolicy.SELECTABLE,
        abortPolicy = AbortPolicy.SAFE_DURING_RUN,
        executionDomain = ExecutionDomain.APP,
        runCapability = RunCapability.DIAGNOSTIC_ONLY,
    )

    /** In-memory complete selection aggregate for stable-key restoration tests. */
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
