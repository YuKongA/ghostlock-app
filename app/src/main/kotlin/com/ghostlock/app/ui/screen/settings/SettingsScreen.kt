package com.ghostlock.app.ui.screen.settings

import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.platform.LocalContext
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import com.ghostlock.app.ui.LocalUiMode
import com.ghostlock.app.ui.UiMode
import com.ghostlock.app.ui.navigation3.Navigator
import com.ghostlock.app.ui.navigation3.Route
import com.ghostlock.app.ui.viewmodel.SettingsViewModel
import com.ghostlock.app.GhostLockApp

@Composable
fun SettingPager(
    navigator: Navigator,
    bottomInnerPadding: Dp,
    isCurrentPage: Boolean = true,
) {
    val app = LocalContext.current.applicationContext as GhostLockApp
    val viewModel = viewModel<SettingsViewModel>(factory = app.container.viewModelFactory)
    val uiState by viewModel.uiState.collectAsStateWithLifecycle()

    val actions = SettingsScreenActions(
        onSetCheckUpdate = viewModel::setCheckUpdate,
        onOpenTheme = { navigator.push(Route.ColorPalette) },
        onSetUiModeIndex = { index ->
            viewModel.setUiMode(if (index == 0) UiMode.Miuix.value else UiMode.Material.value)
        },
        onOpenAbout = { navigator.push(Route.About) },
        onSetLogLevelIndex = viewModel::setLogLevelIndex,
        onSelectTerminalFont = viewModel::selectTerminalFont,
        onSetShowLogTimestamp = viewModel::setShowLogTimestamp,
        onSetAutoExportLogs = viewModel::setAutoExportLogs,
        onSelectLogExportDirectory = viewModel::selectLogExportDirectory,
        onSetWarnOnRerun = viewModel::setWarnOnRerun,
        onSetAutoAttackOnBoot = viewModel::setAutoAttackOnBoot,
        onSetBootRetryCount = viewModel::setBootRetryCount,
        createDiagnosticReport = { app.container.createDiagnosticReport() },
    )

    when (LocalUiMode.current) {
        UiMode.Miuix -> SettingPagerMiuix(uiState, actions, bottomInnerPadding)
        UiMode.Material -> SettingPagerMaterial(uiState, actions, bottomInnerPadding)
    }
}
