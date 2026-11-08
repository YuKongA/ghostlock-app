package com.ghostlock.app.domain.settings

import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.Deferred

/** Observes operational preferences without exposing their Android storage owner. */
class ObserveOperationalPreferencesUseCase(
    private val port: OperationalPreferencesPort,
) {
    /** @return Application-lifetime operational preference stream. */
    operator fun invoke(): StateFlow<OperationalPreferences> = port.preferences
}

/** Saves one actor-current operational preference transform. */
class SaveOperationalPreferencesUseCase(
    private val port: OperationalPreferencesPort,
) {
    /**
     * @param transform Field-preserving transform over the authoritative current state.
     */
    operator fun invoke(
        transform: (OperationalPreferences) -> OperationalPreferences,
    ): Deferred<OperationalPreferences> = port.submit(transform)
}

/** Imports a terminal font selected by the Android document picker. */
class ImportTerminalFontUseCase(
    private val port: OperationalPreferencesPort,
) {
    /**
     * @param uri Serialized picker URI.
     * @return True when the font became the selected terminal font.
     */
    operator fun invoke(uri: String): Deferred<Boolean> = port.importTerminalFont(uri)
}

/** Selects a Downloads-compatible directory returned by the Android tree picker. */
class SelectLogExportDirectoryUseCase(
    private val port: OperationalPreferencesPort,
) {
    /**
     * @param uri Serialized tree URI.
     * @return True when the directory was normalized and persisted.
     */
    operator fun invoke(uri: String): Deferred<Boolean> = port.selectLogExportDirectory(uri)
}
