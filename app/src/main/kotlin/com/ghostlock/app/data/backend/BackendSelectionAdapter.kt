package com.ghostlock.app.data.backend

import com.ghostlock.app.domain.backend.BackendSpec
import com.ghostlock.app.domain.backend.BackendSelectionPort

/**
 * Persists backend selection while hiding registry indices and Android storage from presentation.
 *
 * @param registryEntries Immutable registry-order copy shared with the catalog adapter.
 * @param sessionStore Application-scoped stable-key selection owner.
 */
class BackendSelectionAdapter(
    registryEntries: List<BackendSpec>,
    private val sessionStore: BackendSessionStore,
) : BackendSelectionPort {
    private val entries = registryEntries.toList()

    /** @return True only when the requested captured entry is selectable and becomes current. */
    override fun select(key: String): Boolean {
        val index = entries.indexOfFirst { it.key == key && it.selectable }
        if (index < 0) return false
        sessionStore.select(entries[index])
        return sessionStore.selectedKey == key
    }
}
