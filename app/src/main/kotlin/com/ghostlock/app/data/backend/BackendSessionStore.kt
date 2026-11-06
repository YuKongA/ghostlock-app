package com.ghostlock.app.data.backend

import com.ghostlock.app.domain.backend.BackendSpec

/**
 * Application-scoped selected-backend session over immutable registry keys.
 *
 * Backend configuration is no longer persisted through SharedPreferences or callbacks on BackendSpec. Canonical
 * values are frozen from the version-three GLK profile by the run input freezer; this store owns selection only.
 */
class BackendSessionStore {
    private val lock = Any()
    private var currentKey: String? = null

    /** Current selected stable key. */
    val selectedKey: String?
        get() = synchronized(lock) { currentKey }

    /**
     * Establishes a deterministic selectable fallback for this application process.
     *
     * @param specs Immutable captured registry membership.
     * @return Selected spec, or null when no selectable backend exists.
     */
    fun restore(specs: List<BackendSpec>): BackendSpec? = synchronized(lock) {
        val selected = specs.firstOrNull { spec -> spec.selectable && spec.key == currentKey }
            ?: specs.firstOrNull(BackendSpec::selectable)
        currentKey = selected?.key
        selected
    }

    /**
     * Selects one already-resolved immutable spec.
     *
     * @param spec Candidate spec.
     * @return True only when a selectable spec became current.
     */
    fun select(spec: BackendSpec): Boolean = synchronized(lock) {
        if (!spec.selectable) return@synchronized false
        currentKey = spec.key
        true
    }
}
