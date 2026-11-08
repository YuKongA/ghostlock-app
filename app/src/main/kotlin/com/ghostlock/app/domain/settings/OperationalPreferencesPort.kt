package com.ghostlock.app.domain.settings

import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.Deferred

/**
 * Domain boundary for run/log interaction preferences and Android-owned picker effects.
 *
 * @property preferences Current value plus subsequent adapter-owned changes.
 */
interface OperationalPreferencesPort {
    val preferences: StateFlow<OperationalPreferences>

    /** Submits one actor-current transform and returns its exact committed aggregate. */
    fun submit(
        transform: (OperationalPreferences) -> OperationalPreferences,
    ): Deferred<OperationalPreferences>

    /** Seeds [preferences] from the real store before UI and Run consumers are constructed. */
    suspend fun awaitInitial(): OperationalPreferences

    /** @return Exact immutable operational aggregate at one run cutoff. */
    fun snapshot(): Deferred<OperationalPreferences>

    /**
     * Copies a selected font into application-private storage.
     *
     * @param uri Android URI serialized at the presentation edge.
     * @return True when the adapter copied and selected a readable font.
     */
    fun importTerminalFont(uri: String): Deferred<Boolean>

    /**
     * Selects a Downloads-compatible log export directory.
     *
     * @param uri Android tree URI serialized at the presentation edge.
     * @return True when the tree can be represented by the Downloads export contract.
     */
    fun selectLogExportDirectory(uri: String): Deferred<Boolean>
}
