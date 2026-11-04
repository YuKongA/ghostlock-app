package com.ghostlock.app.ui.viewmodel

import androidx.lifecycle.ViewModel
import com.ghostlock.app.domain.finish.FinishSettings
import com.ghostlock.app.domain.finish.ReadFinishSettingsUseCase
import com.ghostlock.app.domain.finish.SaveFinishSettingsUseCase
import com.ghostlock.app.ui.screen.module.backends.FinishSettingsUiState
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/**
 * Owns finish-settings state without exposing the authoritative storage global to Composables.
 *
 * @param readFinishSettings Reads the two non-payload options.
 * @param saveFinishSettings Saves those options while the adapter preserves all payload fields.
 */
class FinishSettingsViewModel(
    private val readFinishSettings: ReadFinishSettingsUseCase,
    private val saveFinishSettings: SaveFinishSettingsUseCase,
) : ViewModel() {
    private val mutableUiState = MutableStateFlow(readState())
    val uiState: StateFlow<FinishSettingsUiState> = mutableUiState.asStateFlow()

    /** Reloads the persisted finish option snapshot. */
    fun refresh() {
        mutableUiState.value = readState()
    }

    /** Persists SELinux restoration without changing payload or handoff state. */
    fun setRestoreSelinux(enabled: Boolean) = update { copy(restoreSelinux = enabled) }

    /** Persists post-finish soft reboot without changing payload or handoff state. */
    fun setSoftRebootAfterFinish(enabled: Boolean) = update {
        copy(softRebootAfterFinish = enabled)
    }

    /** Saves one atomic settings copy and immediately publishes the resulting UI state. */
    private fun update(transform: FinishSettings.() -> FinishSettings) {
        val next = FinishSettings(
            restoreSelinux = mutableUiState.value.restoreSelinux,
            softRebootAfterFinish = mutableUiState.value.softRebootAfterFinish,
        ).transform()
        saveFinishSettings(next)
        mutableUiState.value = next.toUiState()
    }

    /** @return Current domain snapshot mapped to the renderer contract. */
    private fun readState(): FinishSettingsUiState = readFinishSettings().toUiState()

    /** @return Renderer projection of one domain finish option snapshot. */
    private fun FinishSettings.toUiState(): FinishSettingsUiState = FinishSettingsUiState(
        restoreSelinux = restoreSelinux,
        softRebootAfterFinish = softRebootAfterFinish,
    )
}
