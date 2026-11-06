package com.ghostlock.app.ui.screen.settings

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.WindowInsetsSides
import androidx.compose.foundation.layout.add
import androidx.compose.foundation.layout.displayCutout
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.only
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.systemBars
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.AdminPanelSettings
import androidx.compose.material.icons.automirrored.filled.Article
import androidx.compose.material.icons.filled.Description
import androidx.compose.material.icons.filled.DisplaySettings
import androidx.compose.material.icons.filled.Folder
import androidx.compose.material.icons.filled.SaveAlt
import androidx.compose.material.icons.filled.SystemUpdateAlt
import androidx.compose.material.icons.rounded.BugReport
import androidx.compose.material.icons.rounded.Info
import androidx.compose.material.icons.rounded.Palette
import androidx.compose.material.icons.rounded.RestartAlt
import androidx.compose.material.icons.rounded.Security
import androidx.compose.material.icons.rounded.SystemUpdate
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import com.ghostlock.app.R
import com.ghostlock.app.ui.UiMode
import com.ghostlock.app.ui.component.dialog.rememberLoadingDialog
import com.ghostlock.app.ui.component.miuix.SendLogDialog
import top.yukonga.miuix.kmp.basic.Card
import top.yukonga.miuix.kmp.basic.MiuixScrollBehavior
import top.yukonga.miuix.kmp.basic.Scaffold
import top.yukonga.miuix.kmp.basic.Text
import top.yukonga.miuix.kmp.basic.TopAppBar
import top.yukonga.miuix.kmp.preference.ArrowPreference
import top.yukonga.miuix.kmp.preference.OverlayDropdownPreference
import top.yukonga.miuix.kmp.preference.SwitchPreference
import top.yukonga.miuix.kmp.theme.MiuixTheme.colorScheme
import top.yukonga.miuix.kmp.utils.overScrollVertical
import top.yukonga.miuix.kmp.utils.scrollEndHaptic

/** 设置页（2026-10-18 清理）：只保留通用项、日志、攻击执行、发送日志、关于。 */
@Composable
fun SettingPagerMiuix(uiState: SettingsUiState, actions: SettingsScreenActions, bottomInnerPadding: Dp) {
    val scrollBehavior = MiuixScrollBehavior()
    val loadingDialog = rememberLoadingDialog()
    var showSendLog by rememberSaveable { mutableStateOf(false) }

    Scaffold(
        topBar = {
            TopAppBar(
                title = stringResource(R.string.settings),
                scrollBehavior = scrollBehavior,
            )
        },
        contentWindowInsets = WindowInsets.systemBars.add(WindowInsets.displayCutout).only(WindowInsetsSides.Horizontal),
    ) { innerPadding ->
        Box(modifier = Modifier.padding(innerPadding)) {
            LazyColumn(
                modifier = Modifier
                    .fillMaxHeight()
                    .overScrollVertical()
                    .scrollEndHaptic()
                    .padding(horizontal = 16.dp),
            ) {
                item {
                    Card(modifier = Modifier.padding(top = 12.dp).fillMaxWidth()) {
                        SwitchPreference(
                            startAction = { MiuixLeading(Icons.Filled.SystemUpdateAlt) },
                            title = stringResource(R.string.settings_check_update),
                            summary = stringResource(R.string.settings_check_update_summary),
                            checked = uiState.checkUpdate,
                            onCheckedChange = actions.onSetCheckUpdate,
                        )
                    }
                }
                item {
                    Card(modifier = Modifier.padding(top = 12.dp).fillMaxWidth()) {
                        OverlayDropdownPreference(
                            startAction = { MiuixLeading(Icons.Filled.DisplaySettings) },
                            title = stringResource(R.string.settings_ui_mode),
                            summary = stringResource(R.string.settings_ui_mode_summary),
                            items = UiMode.entries.map { it.name },
                            selectedIndex = if (uiState.uiMode == UiMode.Material.value) 1 else 0,
                            onSelectedIndexChange = actions.onSetUiModeIndex,
                        )
                        ArrowPreference(
                            startAction = { MiuixLeading(Icons.Rounded.Palette) },
                            title = stringResource(R.string.settings_theme),
                            summary = stringResource(R.string.settings_theme_summary),
                            onClick = actions.onOpenTheme,
                        )
                    }
                }
                item { LogPreferencesCardMiuix(uiState, actions) }
                item { RunPreferencesCardMiuix(uiState, actions) }
                item {
                    Card(modifier = Modifier.padding(top = 12.dp, bottom = 12.dp).fillMaxWidth()) {
                        ArrowPreference(
                            title = stringResource(R.string.send_log),
                            startAction = { MiuixLeading(Icons.Rounded.BugReport) },
                            onClick = { showSendLog = true },
                        )
                        SendLogDialog(
                            show = showSendLog,
                            onDismissRequest = { showSendLog = false },
                            loadingDialog = loadingDialog,
                            createReport = actions.createDiagnosticReport,
                        )
                        ArrowPreference(
                            title = stringResource(R.string.about),
                            startAction = { MiuixLeading(Icons.Rounded.Info) },
                            onClick = actions.onOpenAbout,
                        )
                    }
                }
                item { androidx.compose.foundation.layout.Spacer(Modifier.fillMaxWidth().padding(bottom = bottomInnerPadding)) }
            }
        }
    }
}

@Composable
fun LogPreferencesCardMiuix(uiState: SettingsUiState, actions: SettingsScreenActions) {
    val context = LocalContext.current
    val fontPicker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        uri?.let { actions.onSelectTerminalFont(it.toString()) }
    }
    val exportPicker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri ->
        val accepted = uri?.let { actions.onSelectLogExportDirectory(it.toString()) } == true
        if (!accepted && uri != null) {
            android.widget.Toast.makeText(
                context,
                R.string.settings_log_export_location_invalid,
                android.widget.Toast.LENGTH_SHORT,
            ).show()
        }
    }
    val fontSummary = uiState.terminalFontName
        ?: stringResource(R.string.settings_log_font_default)
    Card(modifier = Modifier.padding(top = 12.dp).fillMaxWidth()) {
        OverlayDropdownPreference(
            startAction = { MiuixLeading(Icons.AutoMirrored.Filled.Article) },
            title = stringResource(R.string.settings_log_level),
            summary = stringResource(R.string.settings_log_level_summary),
            items = uiState.logLevelOptions,
            selectedIndex = uiState.selectedLogLevelIndex,
            onSelectedIndexChange = actions.onSetLogLevelIndex,
        )
        ArrowPreference(
            startAction = { MiuixLeading(Icons.Filled.Description) },
            title = stringResource(R.string.settings_log_font),
            summary = fontSummary,
            onClick = { fontPicker.launch(arrayOf("font/ttf", "font/otf", "application/octet-stream")) },
        )
        SwitchPreference(
            startAction = { MiuixLeading(Icons.Rounded.Info) },
            title = stringResource(R.string.settings_log_timestamp),
            summary = stringResource(R.string.settings_log_timestamp_summary),
            checked = uiState.showLogTimestamp,
            onCheckedChange = actions.onSetShowLogTimestamp,
        )
        SwitchPreference(
            startAction = { MiuixLeading(Icons.Filled.SaveAlt) },
            title = stringResource(R.string.settings_log_auto_export),
            summary = stringResource(R.string.settings_log_auto_export_summary),
            checked = uiState.autoExportLogs,
            onCheckedChange = actions.onSetAutoExportLogs,
        )
        ArrowPreference(
            startAction = { MiuixLeading(Icons.Filled.Folder) },
            title = stringResource(R.string.settings_log_export_location),
            summary = uiState.logExportLocation,
            onClick = { exportPicker.launch(null) },
        )
    }
}

@Composable
fun RunPreferencesCardMiuix(uiState: SettingsUiState, actions: SettingsScreenActions) {
    Card(modifier = Modifier.padding(top = 12.dp).fillMaxWidth()) {
        SwitchPreference(
            startAction = { MiuixLeading(Icons.Rounded.Security) },
            title = stringResource(R.string.settings_attack_warn),
            summary = stringResource(R.string.settings_attack_warn_summary),
            checked = uiState.warnOnRerun,
            onCheckedChange = actions.onSetWarnOnRerun,
        )
        SwitchPreference(
            startAction = { MiuixLeading(Icons.Rounded.SystemUpdate) },
            title = stringResource(R.string.settings_boot_attack),
            summary = stringResource(R.string.settings_boot_attack_summary),
            checked = uiState.autoAttackOnBoot,
            onCheckedChange = actions.onSetAutoAttackOnBoot,
        )
        OverlayDropdownPreference(
            startAction = { MiuixLeading(Icons.Rounded.RestartAlt) },
            title = stringResource(R.string.settings_boot_retry),
            summary = stringResource(R.string.settings_boot_retry_summary),
            items = (0..10).map { it.toString() },
            selectedIndex = uiState.bootRetryCount,
            onSelectedIndexChange = actions.onSetBootRetryCount,
        )
    }
}

@Composable
private fun MiuixLeading(icon: androidx.compose.ui.graphics.vector.ImageVector) {
    androidx.compose.material3.Icon(
        imageVector = icon,
        contentDescription = null,
        tint = colorScheme.onBackground,
    )
}
