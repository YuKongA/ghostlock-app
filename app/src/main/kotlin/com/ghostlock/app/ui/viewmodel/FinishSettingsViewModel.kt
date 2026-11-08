package com.ghostlock.app.ui.viewmodel

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.ghostlock.app.domain.finish.FinishSettings
import com.ghostlock.app.domain.finish.ReadFinishSettingsUseCase
import com.ghostlock.app.domain.finish.SaveFinishSettingsUseCase
import com.ghostlock.app.ui.screen.module.finish.FinishSettingsUiState
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

/**
 * Owns finish-settings state without exposing the authoritative storage global to Composables.
 *
 * @param readFinishSettings Reads the two non-payload options.
 * @param saveFinishSettings Saves those options while preserving other Finish actions.
 */
class FinishSettingsViewModel(
    private val readFinishSettings: ReadFinishSettingsUseCase,
    private val saveFinishSettings: SaveFinishSettingsUseCase,
) : ViewModel() {
    private val mutableUiState = MutableStateFlow(
        FinishSettings.from(com.ghostlock.app.domain.finish.defaultFinishDefinition()).toUiState(),
    )
    val uiState: StateFlow<FinishSettingsUiState> = mutableUiState.asStateFlow()

    /** Reloads the persisted finish option snapshot. */
    fun refresh() {
        viewModelScope.launch {
            runCatching { readFinishSettings() }
                .onSuccess { mutableUiState.value = it.toUiState() }
        }
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
        mutableUiState.value = next.toUiState()
        val completion = saveFinishSettings(next)
        viewModelScope.launch {
            if (runCatching { completion.await() }.isFailure) refresh()
        }
    }

    /** @return Renderer projection of one domain finish option snapshot. */
    private fun FinishSettings.toUiState(): FinishSettingsUiState = FinishSettingsUiState(
        restoreSelinux = restoreSelinux,
        softRebootAfterFinish = softRebootAfterFinish,
    )
}
