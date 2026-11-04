package com.ghostlock.app.domain.settings

import kotlinx.coroutines.flow.StateFlow

/**
 * Domain boundary for run/log interaction preferences and Android-owned picker effects.
 *
 * @property preferences Current value plus subsequent adapter-owned changes.
 */
interface OperationalPreferencesPort {
    val preferences: StateFlow<OperationalPreferences>

    /**
     * Persists a complete operational preference snapshot.
     *
     * @param value Storage-neutral state produced by a domain command.
     */
    fun save(value: OperationalPreferences)

    /**
     * Copies a selected font into application-private storage.
     *
     * @param uri Android URI serialized at the presentation edge.
     * @return True when the adapter copied and selected a readable font.
     */
    fun importTerminalFont(uri: String): Boolean

    /**
     * Selects a Downloads-compatible log export directory.
     *
     * @param uri Android tree URI serialized at the presentation edge.
     * @return True when the tree can be represented by the Downloads export contract.
     */
    fun selectLogExportDirectory(uri: String): Boolean
}
