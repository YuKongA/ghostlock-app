package com.ghostlock.app.data.backend

import com.ghostlock.app.domain.backend.BackendDescriptor
import com.ghostlock.app.domain.settings.AppSelectionPort
import com.ghostlock.app.domain.settings.BackendSelectionState

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
     * Restores only an exact durable choice, except for the index default on a genuinely fresh install.
     *
     * @param specs Immutable captured catalog membership.
     * @param defaultBackendKey Explicit authenticated builtin default; list order is never a fallback.
     * @return Selected spec, or null when user choice is required.
     */
    suspend fun restore(
        specs: List<BackendDescriptor>,
        defaultBackendKey: String,
        compatibleBackendKeys: Set<String>,
    ): BackendDescriptor? {
        val current = selectionPort.selection.value
        val selected = when (current.backendSelectionState) {
            BackendSelectionState.FRESH_UNINITIALIZED -> specs.firstOrNull { spec ->
                spec.selectable && spec.key in compatibleBackendKeys && spec.key == defaultBackendKey
            }
            BackendSelectionState.SELECTED -> specs.firstOrNull { spec ->
                spec.selectable && spec.key in compatibleBackendKeys && spec.key == current.selectedBackendKey
            }
            BackendSelectionState.NEEDS_USER_CHOICE -> null
        }
        val next = when {
            selected != null && current.backendSelectionState == BackendSelectionState.FRESH_UNINITIALIZED ->
                current.copy(
                    backendSelectionState = BackendSelectionState.SELECTED,
                    selectedBackendKey = selected.key,
                )
            selected == null && current.backendSelectionState != BackendSelectionState.NEEDS_USER_CHOICE ->
                current.copy(
                    backendSelectionState = BackendSelectionState.NEEDS_USER_CHOICE,
                    selectedBackendKey = null,
                )
            else -> current
        }
        if (next != current) {
            selectionPort.submit { committed ->
                committed.copy(
                    backendSelectionState = next.backendSelectionState,
                    selectedBackendKey = next.selectedBackendKey,
                )
            }.await()
        }
        synchronized(lock) { currentKey = selected?.key }
        return selected
    }

    /** Clears only the process projection when catalog initialization failed; durable intent is preserved. */
    fun clearProcessSelection() {
        synchronized(lock) { currentKey = null }
    }

    /**
     * Selects one already-resolved immutable spec.
     *
     * @param spec Candidate spec.
     * @return True only when a selectable spec became current.
     */
    suspend fun select(spec: BackendDescriptor): Boolean {
        if (!spec.selectable) return false
        selectionPort.submit { current ->
            current.copy(
                backendSelectionState = BackendSelectionState.SELECTED,
                selectedBackendKey = spec.key,
            )
        }.await()
        synchronized(lock) { currentKey = spec.key }
        return true
    }
}
