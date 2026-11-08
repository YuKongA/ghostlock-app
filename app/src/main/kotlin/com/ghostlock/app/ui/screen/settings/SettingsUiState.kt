package com.ghostlock.app.ui.screen.settings

import androidx.compose.runtime.Immutable
import com.materialkolor.PaletteStyle
import com.materialkolor.dynamiccolor.ColorSpec
import com.ghostlock.app.ui.UiMode

@Immutable
data class SettingsUiState(
    val uiMode: String = UiMode.DEFAULT_VALUE,
    val checkUpdate: Boolean = true,
    val themeMode: Int = 0,
    val miuixMonet: Boolean = false,
    val keyColor: Int = 0,
    val colorStyle: String = PaletteStyle.TonalSpot.name,
    val colorSpec: String = ColorSpec.SpecVersion.SPEC_2025.name,
    val enablePredictiveBack: Boolean = false,
    val enableSwipeDismiss: Boolean = true,
    val pagerInterceptionMode: Int = 1,
    val enableBlur: Boolean = true,
    val enableFloatingBottomBar: Boolean = false,
    val enableFloatingBottomBarBlur: Boolean = false,
    val enableNavigationBadge: Boolean = true,
    val pageScale: Float = 1.0f,
    val logLevelOptions: List<String> = listOf("verbose", "debug", "info", "warn", "error"),
    val selectedLogLevelIndex: Int = 2,
    val terminalFontName: String? = null,
    val showLogTimestamp: Boolean = false,
    val autoExportLogs: Boolean = true,
    val logExportLocation: String = "Download/ghostlock-log",
    val warnOnRerun: Boolean = true,
    val autoAttackOnBoot: Boolean = false,
    val bootRetryCount: Int = 1,
)

@Immutable
data class SettingsScreenActions(
    val onSetCheckUpdate: (Boolean) -> Unit,
    val onOpenTheme: () -> Unit,
    val onSetUiModeIndex: (Int) -> Unit,
    val onOpenAbout: () -> Unit,
    val onSetLogLevelIndex: (Int) -> Unit,
    val onSelectTerminalFont: (String, (Boolean) -> Unit) -> Unit,
    val onSetShowLogTimestamp: (Boolean) -> Unit,
    val onSetAutoExportLogs: (Boolean) -> Unit,
    val onSelectLogExportDirectory: (String, (Boolean) -> Unit) -> Unit,
    val onSetWarnOnRerun: (Boolean) -> Unit,
    val onSetAutoAttackOnBoot: (Boolean) -> Unit,
    val onSetBootRetryCount: (Int) -> Unit,
    val createDiagnosticReport: suspend () -> java.io.File,
)
