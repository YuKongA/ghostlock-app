package com.ghostlock.app.data.backend

import com.ghostlock.app.domain.backend.BackendDescriptor
import com.ghostlock.app.domain.backend.BackendSelectionPort

/**
 * Persists backend selection while hiding registry indices and Android storage from presentation.
 *
 * @param registryEntries Immutable descriptor projection from the sole definition registry.
 * @param sessionStore Application-scoped stable-key selection owner.
 */
class BackendSelectionAdapter(
    registryEntries: List<BackendDescriptor>,
    private val sessionStore: BackendSessionStore,
) : BackendSelectionPort {
    private val entries = registryEntries.toList()

    /** @return True only when the requested captured entry is selectable and becomes current. */
    override suspend fun select(key: String): Boolean {
        val index = entries.indexOfFirst { it.key == key && it.selectable }
        if (index < 0) return false
        sessionStore.select(entries[index])
        return sessionStore.selectedKey == key
    }
}
