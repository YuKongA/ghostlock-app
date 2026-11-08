package com.ghostlock.app.domain.settings

import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.Deferred

/**
 * Domain boundary for application-shell preferences.
 *
 * @property preferences Current value plus subsequent storage changes.
 * @note The main-process adapter owns the application-lifetime typed store; callers never access platform persistence.
 */
interface UiPreferencesPort {
    val preferences: StateFlow<UiPreferences>

    /** Submits one actor-current transform without copying a stale observed snapshot. */
    fun submit(transform: (UiPreferences) -> UiPreferences): Deferred<UiPreferences>

    /** Seeds [preferences] with the exact persisted value before UI construction. */
    suspend fun awaitInitial(): UiPreferences

    /** @return Exact immutable value at the current DataStore actor cutoff. */
    fun snapshot(): Deferred<UiPreferences>
}
