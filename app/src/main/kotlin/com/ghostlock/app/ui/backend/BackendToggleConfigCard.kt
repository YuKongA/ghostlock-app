package com.ghostlock.app.ui.backend

import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Security
import androidx.compose.runtime.Composable
import com.ghostlock.app.domain.backend.BackendConfigIcon
import com.ghostlock.app.domain.backend.BackendConfigMutationResult
import com.ghostlock.app.domain.backend.BackendConfiguration
import com.ghostlock.app.ui.kit.ConfigItem
import com.ghostlock.app.ui.kit.LocalUiKit

/** Generic presentation renderer for one complete semantic boolean configuration. */
@Composable
internal fun BackendToggleConfigCard(
    state: BackendConfiguration.Toggle,
    onToggle: (Boolean) -> BackendConfigMutationResult,
) {
    val icon = when (state.icon) {
        BackendConfigIcon.NONE -> null
        BackendConfigIcon.SECURITY -> Icons.Filled.Security
    }
    LocalUiKit.current.ConfigItems(
        listOf(
            ConfigItem.Toggle(
                title = state.title,
                summary = if (state.enabled) state.enabledSummary else state.disabledSummary,
                icon = icon,
                checked = state.enabled,
                onChange = { value -> onToggle(value) },
            ),
        ),
    )
}
