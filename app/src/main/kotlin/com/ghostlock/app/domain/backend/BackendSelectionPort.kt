package com.ghostlock.app.domain.backend

/** Domain command boundary for selecting one captured backend by stable key. */
interface BackendSelectionPort {
    /**
     * Selects and persists one available backend.
     *
     * @param key Stable backend identity from the current catalog.
     * @return True only when the selectable backend became the current selection.
     */
    fun select(key: String): Boolean
}
