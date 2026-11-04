package com.ghostlock.app.domain.settings

import kotlinx.coroutines.flow.StateFlow

/**
 * Domain boundary for application-shell preferences.
 *
 * @property preferences Current value plus subsequent storage changes.
 * @note The data adapter owns listener registration for the application lifetime; callers never access SharedPreferences.
 */
interface UiPreferencesPort {
    val preferences: StateFlow<UiPreferences>

    /**
     * Persists one complete application-shell preference snapshot.
     *
     * @param value Storage-neutral snapshot produced by a domain command.
     * @note Implementations must publish the committed value through [preferences].
     */
    fun save(value: UiPreferences)
}
