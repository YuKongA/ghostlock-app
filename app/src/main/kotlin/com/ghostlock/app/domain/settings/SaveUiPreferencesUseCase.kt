package com.ghostlock.app.domain.settings

/**
 * Commits a caller-produced application-shell preference snapshot through the domain boundary.
 *
 * @param port Storage-independent preference destination.
 * @note The use case accepts only the immutable domain model; Android persistence remains in data.
 */
class SaveUiPreferencesUseCase(
    private val port: UiPreferencesPort,
) {
    /**
     * Persists and publishes the supplied snapshot.
     *
     * @param value Complete application-shell preference state.
     * @note Field validation remains at the command producer so enum-like transitions stay explicit.
     */
    operator fun invoke(value: UiPreferences) {
        port.save(value)
    }
}
