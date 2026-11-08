package com.ghostlock.app.ui.screen.settings

import com.ghostlock.app.ui.icon.Icons as ProjectIcons

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.WindowInsetsSides
import androidx.compose.foundation.layout.add
import androidx.compose.foundation.layout.displayCutout
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.only
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.systemBars
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Icon
import androidx.compose.material3.LargeFlexibleTopAppBar
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.material3.TopAppBarScrollBehavior
import androidx.compose.material3.rememberTopAppBarState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
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
import com.ghostlock.app.ui.component.material.ExpressiveScaffold
import com.ghostlock.app.ui.component.material.SegmentedColumn
import com.ghostlock.app.ui.component.material.SegmentedDropdownItem
import com.ghostlock.app.ui.component.material.SegmentedListItem
import com.ghostlock.app.ui.component.material.SegmentedSwitchItem
import com.ghostlock.app.ui.component.material.SnackBarHost
import com.ghostlock.app.ui.component.material.SendLogBottomSheet
import com.ghostlock.app.ui.component.material.expressiveTopAppBarColors

/** 设置页（2026-10-18 清理）：只保留通用项、日志、攻击执行、发送日志、关于。 */
@Composable
fun SettingPagerMaterial(uiState: SettingsUiState, actions: SettingsScreenActions, bottomInnerPadding: Dp) {
    val scrollBehavior = TopAppBarDefaults.exitUntilCollapsedScrollBehavior(rememberTopAppBarState())
    val snackBarHost = remember { androidx.compose.material3.SnackbarHostState() }
    var showSendLog by remember { mutableStateOf(false) }

    ExpressiveScaffold(
        topBar = { TopBar(scrollBehavior) },
        snackbarHost = { SnackBarHost(Modifier.padding(bottom = bottomInnerPadding), snackBarHost) },
        contentWindowInsets = WindowInsets.safeDrawing.only(WindowInsetsSides.Top + WindowInsetsSides.Horizontal),
    ) { paddingValues ->
        Column(
            modifier = Modifier
                .padding(paddingValues)
                .verticalScroll(rememberScrollState()),
        ) {
            SegmentedColumn(
                modifier = Modifier.padding(start = 16.dp, end = 16.dp, bottom = 13.dp),
                content = listOf({
                    SegmentedSwitchItem(
                        icon = ProjectIcons.Filled.SystemUpdateAlt.material,
                        title = stringResource(R.string.settings_check_update),
                        summary = stringResource(R.string.settings_check_update_summary),
                        checked = uiState.checkUpdate,
                        onCheckedChange = actions.onSetCheckUpdate,
                    )
                }),
            )

            SegmentedColumn(
                modifier = Modifier.padding(start = 16.dp, end = 16.dp, bottom = 13.dp),
                content = listOf(
                    {
                        SegmentedDropdownItem(
                            icon = ProjectIcons.Filled.DisplaySettings.material,
                            title = stringResource(R.string.settings_ui_mode),
                            summary = stringResource(R.string.settings_ui_mode_summary),
                            items = UiMode.entries.map { it.name },
                            selectedIndex = if (uiState.uiMode == UiMode.Material.value) 1 else 0,
                            onItemSelected = actions.onSetUiModeIndex,
                        )
                    },
                    {
                        SegmentedListItem(
                            onClick = actions.onOpenTheme,
                            headlineContent = { Text(stringResource(R.string.settings_theme)) },
                            supportingContent = { Text(stringResource(R.string.settings_theme_summary)) },
                            leadingContent = { Icon(ProjectIcons.Filled.Palette.material, null) },
                            trailingContent = { Icon(ProjectIcons.AutoMirrored.Filled.KeyboardArrowRight.material, null) },
                        )
                    },
                ),
            )

            LogPreferencesCard(uiState, actions)
            RunPreferencesCard(uiState, actions)

            SegmentedColumn(
                modifier = Modifier.padding(start = 16.dp, end = 16.dp, bottom = 13.dp),
                content = listOf(
                    {
                        SegmentedListItem(
                            onClick = { showSendLog = true },
                            headlineContent = { Text(stringResource(R.string.send_log)) },
                            leadingContent = { Icon(ProjectIcons.Filled.BugReport.material, null) },
                        )
                    },
                    {
                        SegmentedListItem(
                            onClick = actions.onOpenAbout,
                            headlineContent = { Text(stringResource(R.string.about)) },
                            leadingContent = { Icon(ProjectIcons.Filled.Info.material, null) },
                        )
                    },
                ),
            )

            Spacer(Modifier.height(bottomInnerPadding))

            if (showSendLog) {
                SendLogBottomSheet(
                    onDismiss = { showSendLog = false },
                    snackbarHostState = snackBarHost,
                    createReport = actions.createDiagnosticReport,
                )
            }
        }
    }
}

@Composable
private fun TopBar(scrollBehavior: TopAppBarScrollBehavior?) {
    LargeFlexibleTopAppBar(
        title = { Text(stringResource(R.string.settings)) },
        colors = expressiveTopAppBarColors(),
        windowInsets = WindowInsets.safeDrawing.only(WindowInsetsSides.Top + WindowInsetsSides.Horizontal),
        scrollBehavior = scrollBehavior,
    )
}

@Composable
fun LogPreferencesCard(uiState: SettingsUiState, actions: SettingsScreenActions) {
    val context = LocalContext.current
    val fontPicker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        uri?.let { selected -> actions.onSelectTerminalFont(selected.toString()) { } }
    }
    val exportPicker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri ->
        uri?.let { selected ->
            actions.onSelectLogExportDirectory(selected.toString()) { accepted ->
                if (!accepted) {
                    android.widget.Toast.makeText(
                        context,
                        R.string.settings_log_export_location_invalid,
                        android.widget.Toast.LENGTH_SHORT,
                    ).show()
                }
            }
        }
    }
    val fontSummary = uiState.terminalFontName
        ?: stringResource(R.string.settings_log_font_default)
    SegmentedColumn(
        modifier = Modifier.padding(start = 16.dp, end = 16.dp, bottom = 13.dp),
        content = listOf(
            {
                SegmentedDropdownItem(
                    icon = ProjectIcons.AutoMirrored.Filled.Article.material,
                    title = stringResource(R.string.settings_log_level),
                    summary = stringResource(R.string.settings_log_level_summary),
                    items = uiState.logLevelOptions,
                    selectedIndex = uiState.selectedLogLevelIndex,
                    onItemSelected = actions.onSetLogLevelIndex,
                )
            },
            {
                SegmentedListItem(
                    onClick = { fontPicker.launch(arrayOf("font/ttf", "font/otf", "application/octet-stream")) },
                    headlineContent = { Text(stringResource(R.string.settings_log_font)) },
                    supportingContent = { Text(fontSummary) },
                    leadingContent = { Icon(ProjectIcons.Filled.Description.material, null) },
                    trailingContent = { Icon(ProjectIcons.AutoMirrored.Filled.KeyboardArrowRight.material, null) },
                )
            },
            {
                SegmentedSwitchItem(
                    icon = ProjectIcons.Filled.Info.material,
                    title = stringResource(R.string.settings_log_timestamp),
                    summary = stringResource(R.string.settings_log_timestamp_summary),
                    checked = uiState.showLogTimestamp,
                    onCheckedChange = actions.onSetShowLogTimestamp,
                )
            },
            {
                SegmentedSwitchItem(
                    icon = ProjectIcons.Filled.SaveAlt.material,
                    title = stringResource(R.string.settings_log_auto_export),
                    summary = stringResource(R.string.settings_log_auto_export_summary),
                    checked = uiState.autoExportLogs,
                    onCheckedChange = actions.onSetAutoExportLogs,
                )
            },
            {
                SegmentedListItem(
                    onClick = { exportPicker.launch(null) },
                    headlineContent = { Text(stringResource(R.string.settings_log_export_location)) },
                    supportingContent = { Text(uiState.logExportLocation) },
                    leadingContent = { Icon(ProjectIcons.Filled.Folder.material, null) },
                    trailingContent = { Icon(ProjectIcons.AutoMirrored.Filled.KeyboardArrowRight.material, null) },
                )
            },
        ),
    )
}

@Composable
fun RunPreferencesCard(uiState: SettingsUiState, actions: SettingsScreenActions) {
    val retryItems = (0..10).map { it.toString() }
    SegmentedColumn(
        modifier = Modifier.padding(start = 16.dp, end = 16.dp, bottom = 13.dp),
        content = listOf(
            {
                SegmentedSwitchItem(
                    icon = ProjectIcons.Filled.Security.material,
                    title = stringResource(R.string.settings_attack_warn),
                    summary = stringResource(R.string.settings_attack_warn_summary),
                    checked = uiState.warnOnRerun,
                    onCheckedChange = actions.onSetWarnOnRerun,
                )
            },
            {
                SegmentedSwitchItem(
                    icon = ProjectIcons.Filled.SystemUpdate.material,
                    title = stringResource(R.string.settings_boot_attack),
                    summary = stringResource(R.string.settings_boot_attack_summary),
                    checked = uiState.autoAttackOnBoot,
                    onCheckedChange = actions.onSetAutoAttackOnBoot,
                )
            },
            {
                SegmentedDropdownItem(
                    icon = ProjectIcons.Filled.RestartAlt.material,
                    title = stringResource(R.string.settings_boot_retry),
                    summary = stringResource(R.string.settings_boot_retry_summary),
                    items = retryItems,
                    selectedIndex = uiState.bootRetryCount,
                    onItemSelected = actions.onSetBootRetryCount,
                )
            },
        ),
    )
}
