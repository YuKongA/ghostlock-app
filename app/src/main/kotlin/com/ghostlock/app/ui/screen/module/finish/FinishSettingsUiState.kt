package com.ghostlock.app.ui.screen.module.finish

import androidx.compose.runtime.Immutable

/** Shared semantic state for finish-settings cards and both page skins. */
@Immutable
data class FinishSettingsUiState(
    val restoreSelinux: Boolean = false,
    val softRebootAfterFinish: Boolean = false,
)

/** Shared finish-settings interactions; renderers contain no persistence calls. */
@Immutable
data class FinishSettingsActions(
    val onBack: () -> Unit,
    val onSetRestoreSelinux: (Boolean) -> Unit,
    val onSetSoftRebootAfterFinish: (Boolean) -> Unit,
)
