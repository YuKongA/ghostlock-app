package com.ghostlock.app.domain.settings

import kotlinx.coroutines.flow.StateFlow

/**
 * Exposes application-shell preference changes through the domain boundary.
 *
 * @param port Storage-independent preference source.
 * @note Collection does not mutate preferences and remains active for the collecting lifecycle.
 */
class ObserveUiPreferencesUseCase(
    private val port: UiPreferencesPort,
) {
    /**
     * Returns the current preferences and future changes.
     *
     * @return Hot state owned by the injected port.
     * @note The call has no failure fallback; adapter construction must succeed at composition startup.
     */
    operator fun invoke(): StateFlow<UiPreferences> = port.preferences
}
