package com.ghostlock.app.data.backend

import com.ghostlock.app.composition.backend.BackendCatalogView
import com.ghostlock.app.domain.backend.BackendSelectionPort

/**
 * Persists backend selection while hiding registry indices and Android storage from presentation.
 *
 * @param catalogView Reads the one terminal READY catalog generation without copying membership.
 * @param sessionStore Application-scoped stable-key selection owner.
 */
class BackendSelectionAdapter(
    private val catalogView: () -> BackendCatalogView?,
    private val sessionStore: BackendSessionStore,
) : BackendSelectionPort {
    /** @return True only when the requested READY-catalog entry is selectable and becomes current. */
    override suspend fun select(key: String): Boolean {
        val projection = catalogView()?.resolve(key)
            ?.takeIf { it.descriptor.selectable && it.availability.isReady }
            ?: return false
        sessionStore.select(projection.descriptor)
        return sessionStore.selectedKey == key
    }
}
