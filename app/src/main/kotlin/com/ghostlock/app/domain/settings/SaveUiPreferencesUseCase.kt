package com.ghostlock.app.domain.settings

import kotlinx.coroutines.Deferred

/**
 * Commits an actor-current application-shell preference transform through the domain boundary.
 *
 * @param port Storage-independent preference destination.
 * @note The use case accepts only the immutable domain model; Android persistence remains in data.
 */
class SaveUiPreferencesUseCase(
    private val port: UiPreferencesPort,
) {
    /**
     * Persists and publishes the supplied transform result.
     *
     * @param transform Field-preserving transform over the authoritative current state.
     * @note Field validation remains at the command producer so enum-like transitions stay explicit.
     */
    operator fun invoke(
        transform: (UiPreferences) -> UiPreferences,
    ): Deferred<UiPreferences> = port.submit(transform)
}
