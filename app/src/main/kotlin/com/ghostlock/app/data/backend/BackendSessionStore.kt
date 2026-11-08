package com.ghostlock.app.data.backend

import com.ghostlock.app.domain.backend.BackendDescriptor
import com.ghostlock.app.domain.settings.AppSelectionPort

/**
 * Application-scoped selected-backend session over immutable registry keys.
 *
 * Backend configuration is not owned by this class or callbacks on BackendDescriptor. Kernel-offset backends freeze
 * their GLK profile separately; this store owns only the validated process-local selection projected from AppSelection.
 */
class BackendSessionStore(
    private val selectionPort: AppSelectionPort,
) {
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
    suspend fun restore(specs: List<BackendDescriptor>): BackendDescriptor? {
        val requestedKey = selectionPort.selection.value.selectedBackendKey
        val selected = specs.firstOrNull { spec -> spec.selectable && spec.key == requestedKey }
            ?: specs.firstOrNull(BackendDescriptor::selectable)
        val selectedKey = selected?.key
        if (selectedKey != requestedKey) {
            selectionPort.submit { current -> current.copy(selectedBackendKey = selectedKey) }.await()
        }
        synchronized(lock) { currentKey = selectedKey }
        return selected
    }

    /**
     * Selects one already-resolved immutable spec.
     *
     * @param spec Candidate spec.
     * @return True only when a selectable spec became current.
     */
    suspend fun select(spec: BackendDescriptor): Boolean {
        if (!spec.selectable) return false
        selectionPort.submit { current -> current.copy(selectedBackendKey = spec.key) }.await()
        synchronized(lock) { currentKey = spec.key }
        return true
    }
}
