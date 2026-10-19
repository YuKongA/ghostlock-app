package com.ghostlock.app.ui.screen.settings

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
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.Article
import androidx.compose.material.icons.automirrored.filled.KeyboardArrowRight
import androidx.compose.material.icons.filled.BugReport
import androidx.compose.material.icons.filled.Description
import androidx.compose.material.icons.filled.DisplaySettings
import androidx.compose.material.icons.filled.Folder
import androidx.compose.material.icons.filled.Info
import androidx.compose.material.icons.filled.SaveAlt
import androidx.compose.material.icons.filled.Palette
import androidx.compose.material.icons.filled.RestartAlt
import androidx.compose.material.icons.filled.Security
import androidx.compose.material.icons.filled.SystemUpdate
import androidx.compose.material.icons.filled.SystemUpdateAlt
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
import com.ghostlock.app.attack.AttackSettings
import com.ghostlock.app.attack.BootSettings
import com.ghostlock.app.log.LogLevel
import com.ghostlock.app.log.LogSettings
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
                        icon = Icons.Filled.SystemUpdateAlt,
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
                            icon = Icons.Filled.DisplaySettings,
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
                            leadingContent = { Icon(Icons.Filled.Palette, null) },
                            trailingContent = { Icon(Icons.AutoMirrored.Filled.KeyboardArrowRight, null) },
                        )
                    },
                ),
            )

            LogSettingsCard()
            AttackSettingsCard()

            SegmentedColumn(
                modifier = Modifier.padding(start = 16.dp, end = 16.dp, bottom = 13.dp),
                content = listOf(
                    {
                        SegmentedListItem(
                            onClick = { showSendLog = true },
                            headlineContent = { Text(stringResource(R.string.send_log)) },
                            leadingContent = { Icon(Icons.Filled.BugReport, null) },
                        )
                    },
                    {
                        SegmentedListItem(
                            onClick = actions.onOpenAbout,
                            headlineContent = { Text(stringResource(R.string.about)) },
                            leadingContent = { Icon(Icons.Filled.Info, null) },
                        )
                    },
                ),
            )

            Spacer(Modifier.height(bottomInnerPadding))

            if (showSendLog) {
                SendLogBottomSheet(onDismiss = { showSendLog = false }, snackbarHostState = snackBarHost)
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
fun LogSettingsCard() {
    val context = LocalContext.current
    val fontPicker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        uri?.let { LogSettings.setFont(context, it) }
    }
    val exportPicker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri ->
        val path = uri?.let(LogSettings::exportLocationFromTree)
        if (path != null) LogSettings.setExportLocation(context, path)
        else android.widget.Toast.makeText(context, R.string.settings_log_export_location_invalid, android.widget.Toast.LENGTH_SHORT).show()
    }
    val levels = LogLevel.entries
    val fontSummary = LogSettings.fontPath?.let { java.io.File(it).name } ?: stringResource(R.string.settings_log_font_default)
    SegmentedColumn(
        modifier = Modifier.padding(start = 16.dp, end = 16.dp, bottom = 13.dp),
        content = listOf(
            {
                SegmentedDropdownItem(
                    icon = Icons.AutoMirrored.Filled.Article,
                    title = stringResource(R.string.settings_log_level),
                    summary = stringResource(R.string.settings_log_level_summary),
                    items = levels.map { it.token },
                    selectedIndex = levels.indexOf(LogSettings.level).coerceAtLeast(0),
                    onItemSelected = { index -> LogSettings.setLevel(context, levels[index]) },
                )
            },
            {
                SegmentedListItem(
                    onClick = { fontPicker.launch(arrayOf("font/ttf", "font/otf", "application/octet-stream")) },
                    headlineContent = { Text(stringResource(R.string.settings_log_font)) },
                    supportingContent = { Text(fontSummary) },
                    leadingContent = { Icon(Icons.Filled.Description, null) },
                    trailingContent = { Icon(Icons.AutoMirrored.Filled.KeyboardArrowRight, null) },
                )
            },
            {
                SegmentedSwitchItem(
                    icon = Icons.Filled.Info,
                    title = stringResource(R.string.settings_log_timestamp),
                    summary = stringResource(R.string.settings_log_timestamp_summary),
                    checked = LogSettings.showTimestamp,
                    onCheckedChange = { LogSettings.setShowTimestamp(context, it) },
                )
            },
            {
                SegmentedSwitchItem(
                    icon = Icons.Filled.SaveAlt,
                    title = stringResource(R.string.settings_log_auto_export),
                    summary = stringResource(R.string.settings_log_auto_export_summary),
                    checked = LogSettings.autoExport,
                    onCheckedChange = { LogSettings.setAutoExport(context, it) },
                )
            },
            {
                SegmentedListItem(
                    onClick = { exportPicker.launch(null) },
                    headlineContent = { Text(stringResource(R.string.settings_log_export_location)) },
                    supportingContent = { Text(LogSettings.exportLocation) },
                    leadingContent = { Icon(Icons.Filled.Folder, null) },
                    trailingContent = { Icon(Icons.AutoMirrored.Filled.KeyboardArrowRight, null) },
                )
            },
        ),
    )
}

@Composable
fun AttackSettingsCard() {
    val context = LocalContext.current
    val retryItems = (0..10).map { it.toString() }
    SegmentedColumn(
        modifier = Modifier.padding(start = 16.dp, end = 16.dp, bottom = 13.dp),
        content = listOf(
            {
                SegmentedSwitchItem(
                    icon = Icons.Filled.Security,
                    title = stringResource(R.string.settings_attack_warn),
                    summary = stringResource(R.string.settings_attack_warn_summary),
                    checked = AttackSettings.warnOnRerun,
                    onCheckedChange = { AttackSettings.setWarnOnRerun(context, it) },
                )
            },
            {
                SegmentedSwitchItem(
                    icon = Icons.Filled.SystemUpdate,
                    title = stringResource(R.string.settings_boot_attack),
                    summary = stringResource(R.string.settings_boot_attack_summary),
                    checked = BootSettings.autoAttackOnBoot,
                    onCheckedChange = { BootSettings.setAutoAttackOnBoot(context, it) },
                )
            },
            {
                SegmentedDropdownItem(
                    icon = Icons.Filled.RestartAlt,
                    title = stringResource(R.string.settings_boot_retry),
                    summary = stringResource(R.string.settings_boot_retry_summary),
                    items = retryItems,
                    selectedIndex = BootSettings.bootRetryCount,
                    onItemSelected = { BootSettings.setBootRetryCount(context, it) },
                )
            },
        ),
    )
}
