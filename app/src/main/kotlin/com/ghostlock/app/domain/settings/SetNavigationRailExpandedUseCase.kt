package com.ghostlock.app.domain.settings

/**
 * Persists the application-shell navigation-rail state without exposing its storage adapter.
 *
 * @param port Storage-independent application-shell preference boundary.
 * @note This command only changes the navigation-rail field and leaves every other preference intact.
 */
class SetNavigationRailExpandedUseCase(
    private val port: UiPreferencesPort,
) {
    /**
     * Writes the latest navigation-rail state.
     *
     * @param expanded True when the next wide-screen shell should start expanded.
     * @note Persistence completion is adapter-defined; the domain stream publishes the committed snapshot.
     */
    operator fun invoke(expanded: Boolean) {
        port.save(port.preferences.value.copy(navigationRailExpanded = expanded))
    }
}
