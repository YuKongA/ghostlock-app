package com.ghostlock.app.ui.screen.module.backends

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
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.ghostlock.app.R
import com.ghostlock.app.ui.component.material.ExpressiveScaffold
import com.ghostlock.app.ui.component.material.SegmentedColumn
import com.ghostlock.app.ui.component.material.SegmentedSwitchItem
import com.ghostlock.app.ui.component.material.TonalCard
import com.ghostlock.app.ui.component.material.expressiveTopAppBarColors
import com.ghostlock.app.ui.PageChrome
import top.yukonga.miuix.kmp.basic.Card
import top.yukonga.miuix.kmp.basic.MiuixScrollBehavior
import top.yukonga.miuix.kmp.basic.Scaffold as MiuixScaffold
import top.yukonga.miuix.kmp.basic.TopAppBar as MiuixTopAppBar
import top.yukonga.miuix.kmp.preference.SwitchPreference
import top.yukonga.miuix.kmp.theme.MiuixTheme

/** 收尾菜单：只保留 SELinux 与软重启；不构建脚本。 */
@Composable
fun FinishSettingsPage(miuix: Boolean, bottomInnerPadding: Dp, onBack: () -> Unit) {
    val state = FinishConfig.state
    val context = androidx.compose.ui.platform.LocalContext.current
    if (miuix) {
        val scroll = MiuixScrollBehavior()
        MiuixScaffold(
            topBar = {
                MiuixTopAppBar(
                    title = stringResource(R.string.finish_settings_title),
                    navigationIcon = {
                        androidx.compose.material3.IconButton(onClick = onBack) {
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
                        onCheckedChange = {
                            val next = state.copy(restoreSelinux = it)
                            FinishConfig.update(context, next)
                        },
                        startAction = { Icon(Icons.Filled.Security, null, tint = MiuixTheme.colorScheme.onBackground) },
                    )
                    SwitchPreference(
                        title = stringResource(R.string.finish_soft_reboot),
                        summary = stringResource(R.string.finish_soft_reboot_summary),
                        checked = state.softRebootAfterFinish,
                        onCheckedChange = {
                            val next = state.copy(softRebootAfterFinish = it)
                            FinishConfig.update(context, next)
                        },
                        startAction = { Icon(Icons.Filled.RestartAlt, null, tint = MiuixTheme.colorScheme.onBackground) },
                    )
                }
            }
        }
    } else {
        val scroll = TopAppBarDefaults.exitUntilCollapsedScrollBehavior(rememberTopAppBarState())
        ExpressiveScaffold(
            topBar = {
                LargeFlexibleTopAppBar(
                    title = { Text(stringResource(R.string.finish_settings_title)) },
                    colors = expressiveTopAppBarColors(),
                    scrollBehavior = scroll,
                    navigationIcon = {
                        androidx.compose.material3.IconButton(onClick = onBack) {
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
                                onCheckedChange = {
                                    val next = state.copy(restoreSelinux = it)
                                    FinishConfig.update(context, next)
                                },
                            )
                        }
                        item {
                            SegmentedSwitchItem(
                                icon = Icons.Filled.RestartAlt,
                                title = stringResource(R.string.finish_soft_reboot),
                                summary = stringResource(R.string.finish_soft_reboot_summary),
                                checked = state.softRebootAfterFinish,
                                onCheckedChange = {
                                    val next = state.copy(softRebootAfterFinish = it)
                                    FinishConfig.update(context, next)
                                },
                            )
                        }
                    }
                }
            }
        }
    }
}

@Composable
fun FinishSettingsCard(miuix: Boolean, onOpen: () -> Unit) {
    val state = FinishConfig.state
    val enabled = buildList {
        if (state.restoreSelinux) add(stringResource(R.string.finish_restore_selinux))
        if (state.softRebootAfterFinish) add(stringResource(R.string.finish_soft_reboot))
    }
    val summary = when (enabled.size) {
        0 -> stringResource(R.string.finish_none)
        else -> enabled.joinToString(" · ")
    }
    if (miuix) {
        Card(Modifier.fillMaxWidth()) {
            top.yukonga.miuix.kmp.preference.ArrowPreference(
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
    } else {
        TonalCard {
            SegmentedColumn(Modifier.fillMaxWidth()) {
                item {
                    com.ghostlock.app.ui.component.material.SegmentedListItem(
                        onClick = onOpen,
                        headlineContent = { Text(stringResource(R.string.finish_settings_title)) },
                        supportingContent = { Text(summary) },
                        leadingContent = { Icon(Icons.Filled.RestartAlt, null) },
                    )
                }
            }
        }
    }
}
