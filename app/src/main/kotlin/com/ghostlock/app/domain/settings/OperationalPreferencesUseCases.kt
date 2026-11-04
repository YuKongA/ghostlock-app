package com.ghostlock.app.domain.settings

import kotlinx.coroutines.flow.StateFlow

/** Observes operational preferences without exposing their Android storage owner. */
class ObserveOperationalPreferencesUseCase(
    private val port: OperationalPreferencesPort,
) {
    /** @return Application-lifetime operational preference stream. */
    operator fun invoke(): StateFlow<OperationalPreferences> = port.preferences
}

/** Saves one immutable operational preference snapshot. */
class SaveOperationalPreferencesUseCase(
    private val port: OperationalPreferencesPort,
) {
    /**
     * @param value Complete storage-neutral state.
     */
    operator fun invoke(value: OperationalPreferences) = port.save(value)
}

/** Imports a terminal font selected by the Android document picker. */
class ImportTerminalFontUseCase(
    private val port: OperationalPreferencesPort,
) {
    /**
     * @param uri Serialized picker URI.
     * @return True when the font became the selected terminal font.
     */
    operator fun invoke(uri: String): Boolean = port.importTerminalFont(uri)
}

/** Selects a Downloads-compatible directory returned by the Android tree picker. */
class SelectLogExportDirectoryUseCase(
    private val port: OperationalPreferencesPort,
) {
    /**
     * @param uri Serialized tree URI.
     * @return True when the directory was normalized and persisted.
     */
    operator fun invoke(uri: String): Boolean = port.selectLogExportDirectory(uri)
}
