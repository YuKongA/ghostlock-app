package com.ghostlock.app.ui.screen.module.finish

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.RestartAlt
import androidx.compose.material.icons.filled.Security
import androidx.compose.material3.Icon
import androidx.compose.material3.LargeFlexibleTopAppBar
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.material3.rememberTopAppBarState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import com.ghostlock.app.GhostLockApp
import com.ghostlock.app.R
import com.ghostlock.app.ui.LocalUiMode
import com.ghostlock.app.ui.UiMode
import com.ghostlock.app.ui.component.material.ExpressiveScaffold
import com.ghostlock.app.ui.component.material.SegmentedColumn
import com.ghostlock.app.ui.component.material.SegmentedListItem
import com.ghostlock.app.ui.component.material.SegmentedSwitchItem
import com.ghostlock.app.ui.component.material.TonalCard
import com.ghostlock.app.ui.component.material.expressiveTopAppBarColors
import com.ghostlock.app.ui.viewmodel.FinishSettingsViewModel
import top.yukonga.miuix.kmp.basic.Card
import top.yukonga.miuix.kmp.basic.MiuixScrollBehavior
import top.yukonga.miuix.kmp.basic.Scaffold as MiuixScaffold
import top.yukonga.miuix.kmp.basic.TopAppBar as MiuixTopAppBar
import top.yukonga.miuix.kmp.preference.ArrowPreference
import top.yukonga.miuix.kmp.preference.SwitchPreference
import top.yukonga.miuix.kmp.theme.MiuixTheme

/**
 * Composes finish settings from one shared state/action contract and the requested existing skin.
 *
 * @param bottomInnerPadding Bottom chrome inset supplied by the parent page.
 * @param onBack Navigation action owned by the caller.
 */
@Composable
fun FinishSettingsPage(bottomInnerPadding: Dp, onBack: () -> Unit) {
    val viewModel = finishSettingsViewModel()
    val state by viewModel.uiState.collectAsStateWithLifecycle()
    LaunchedEffect(Unit) { viewModel.refresh() }
    val actions = FinishSettingsActions(
        onBack = onBack,
        onSetRestoreSelinux = viewModel::setRestoreSelinux,
        onSetSoftRebootAfterFinish = viewModel::setSoftRebootAfterFinish,
    )
    when (LocalUiMode.current) {
        UiMode.Material -> FinishSettingsPageMaterial(state, actions, bottomInnerPadding)
        UiMode.Miuix -> FinishSettingsPageMiuix(state, actions, bottomInnerPadding)
    }
}

/** Renders the existing Miuix finish-settings appearance from semantic inputs only. */
@Composable
private fun FinishSettingsPageMiuix(
    state: FinishSettingsUiState,
    actions: FinishSettingsActions,
    bottomInnerPadding: Dp,
) {
    val scroll = MiuixScrollBehavior()
    MiuixScaffold(
        topBar = {
            MiuixTopAppBar(
                title = stringResource(R.string.finish_settings_title),
                navigationIcon = {
                    androidx.compose.material3.IconButton(onClick = actions.onBack) {
                        Icon(
                            Icons.AutoMirrored.Filled.ArrowBack,
                            contentDescription = stringResource(R.string.action_back),
                            tint = MiuixTheme.colorScheme.onBackground,
                        )
                    }
                },
                scrollBehavior = scroll,
            )
        },
    ) { inner ->
        Column(
            modifier = Modifier.fillMaxSize().padding(inner)
                .verticalScroll(rememberScrollState())
                .padding(horizontal = 16.dp, vertical = 12.dp)
                .padding(bottom = bottomInnerPadding),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            Card(Modifier.fillMaxWidth()) {
                SwitchPreference(
                    title = stringResource(R.string.finish_restore_selinux),
                    summary = stringResource(R.string.finish_restore_selinux_summary),
                    checked = state.restoreSelinux,
                    onCheckedChange = actions.onSetRestoreSelinux,
                    startAction = {
                        Icon(Icons.Filled.Security, null, tint = MiuixTheme.colorScheme.onBackground)
                    },
                )
                SwitchPreference(
                    title = stringResource(R.string.finish_soft_reboot),
                    summary = stringResource(R.string.finish_soft_reboot_summary),
                    checked = state.softRebootAfterFinish,
                    onCheckedChange = actions.onSetSoftRebootAfterFinish,
                    startAction = {
                        Icon(Icons.Filled.RestartAlt, null, tint = MiuixTheme.colorScheme.onBackground)
                    },
                )
            }
        }
    }
}

/** Renders the existing Material finish-settings appearance from semantic inputs only. */
@Composable
private fun FinishSettingsPageMaterial(
    state: FinishSettingsUiState,
    actions: FinishSettingsActions,
    bottomInnerPadding: Dp,
) {
    val scroll = TopAppBarDefaults.exitUntilCollapsedScrollBehavior(rememberTopAppBarState())
    ExpressiveScaffold(
        topBar = {
            LargeFlexibleTopAppBar(
                title = { Text(stringResource(R.string.finish_settings_title)) },
                colors = expressiveTopAppBarColors(),
                scrollBehavior = scroll,
                navigationIcon = {
                    androidx.compose.material3.IconButton(onClick = actions.onBack) {
                        Icon(
                            Icons.AutoMirrored.Filled.ArrowBack,
                            contentDescription = stringResource(R.string.action_back),
                        )
                    }
                },
            )
        },
    ) { inner ->
        Column(
            modifier = Modifier.fillMaxSize().padding(inner)
                .verticalScroll(rememberScrollState())
                .padding(horizontal = 16.dp, vertical = 12.dp)
                .padding(bottom = bottomInnerPadding),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            TonalCard {
                SegmentedColumn(Modifier.fillMaxWidth()) {
                    item {
                        SegmentedSwitchItem(
                            icon = Icons.Filled.Security,
                            title = stringResource(R.string.finish_restore_selinux),
                            summary = stringResource(R.string.finish_restore_selinux_summary),
                            checked = state.restoreSelinux,
                            onCheckedChange = actions.onSetRestoreSelinux,
                        )
                    }
                    item {
                        SegmentedSwitchItem(
                            icon = Icons.Filled.RestartAlt,
                            title = stringResource(R.string.finish_soft_reboot),
                            summary = stringResource(R.string.finish_soft_reboot_summary),
                            checked = state.softRebootAfterFinish,
                            onCheckedChange = actions.onSetSoftRebootAfterFinish,
                        )
                    }
                }
            }
        }
    }
}

/** Renders the Material path-page summary card from shared finish-settings state. */
@Composable
fun FinishSettingsCardMaterial(state: FinishSettingsUiState, onOpen: () -> Unit) {
    val enabled = buildList {
        if (state.restoreSelinux) add(stringResource(R.string.finish_restore_selinux))
        if (state.softRebootAfterFinish) add(stringResource(R.string.finish_soft_reboot))
    }
    val summary = when (enabled.size) {
        0 -> stringResource(R.string.finish_none)
        else -> enabled.joinToString(" · ")
    }
    TonalCard {
        SegmentedColumn(Modifier.fillMaxWidth()) {
            item {
                SegmentedListItem(
                    onClick = onOpen,
                    headlineContent = { Text(stringResource(R.string.finish_settings_title)) },
                    supportingContent = { Text(summary) },
                    leadingContent = { Icon(Icons.Filled.RestartAlt, null) },
                )
            }
        }
    }
}

/** Renders the Miuix path-page summary card from the same semantic state. */
@Composable
fun FinishSettingsCardMiuix(state: FinishSettingsUiState, onOpen: () -> Unit) {
    val enabled = buildList {
        if (state.restoreSelinux) add(stringResource(R.string.finish_restore_selinux))
        if (state.softRebootAfterFinish) add(stringResource(R.string.finish_soft_reboot))
    }
    val summary = if (enabled.isEmpty()) {
        stringResource(R.string.finish_none)
    } else {
        enabled.joinToString(" · ")
    }
    Card(Modifier.fillMaxWidth()) {
        ArrowPreference(
            title = stringResource(R.string.finish_settings_title),
            summary = summary,
            onClick = onOpen,
            startAction = {
                Icon(
                    Icons.Filled.RestartAlt,
                    contentDescription = null,
                    tint = MiuixTheme.colorScheme.onBackground,
                )
            },
        )
    }
}

/** @return Activity-scoped injected finish-settings ViewModel. */
@Composable
private fun finishSettingsViewModel(): FinishSettingsViewModel {
    val app = LocalContext.current.applicationContext as GhostLockApp
    return viewModel(factory = app.container.viewModelFactory)
}
