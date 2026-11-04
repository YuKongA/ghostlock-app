package com.ghostlock.app.data.backend

import android.content.Context
import com.ghostlock.app.backend.declarations.BackendSpec
import com.ghostlock.app.domain.backend.BackendSelectionPort

/**
 * Persists backend selection while hiding registry indices and Android storage from presentation.
 *
 * @param context Application context used by the existing backend session store.
 * @param registryEntries Immutable registry-order copy shared with the catalog adapter.
 * @param sessionStore Application-scoped selection and configuration persistence owner.
 */
class BackendSelectionAdapter(
    context: Context,
    registryEntries: List<BackendSpec>,
    private val sessionStore: BackendSessionStore,
) : BackendSelectionPort {
    private val applicationContext = context.applicationContext
    private val entries = registryEntries.toList()

    /** @return True only when the requested captured entry is selectable and becomes current. */
    override fun select(key: String): Boolean {
        val index = entries.indexOfFirst { it.key == key && it.selectable }
        if (index < 0) return false
        sessionStore.select(applicationContext, entries[index])
        return sessionStore.selectedKey == key
    }
}
