package com.ghostlock.app.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import com.ghostlock.app.R
import top.yukonga.miuix.kmp.basic.Card
import top.yukonga.miuix.kmp.basic.Icon
import top.yukonga.miuix.kmp.basic.IconButton
import top.yukonga.miuix.kmp.basic.MiuixScrollBehavior
import top.yukonga.miuix.kmp.basic.Scaffold
import top.yukonga.miuix.kmp.basic.SmallTitle
import top.yukonga.miuix.kmp.basic.Text
import top.yukonga.miuix.kmp.basic.TopAppBar
import top.yukonga.miuix.kmp.basic.rememberTopAppBarState
import top.yukonga.miuix.kmp.icon.MiuixIcons
import top.yukonga.miuix.kmp.icon.extended.Back
import top.yukonga.miuix.kmp.preference.ArrowPreference
import top.yukonga.miuix.kmp.preference.SwitchPreference
import top.yukonga.miuix.kmp.theme.MiuixTheme

/**
 * P1 plugin settings page (interface freeze 2026-10-05).
 *
 * The page is a pure projection of [GhostlockUiState.pluginRows] (see
 * [pluginRows] in PluginPresentation for the rules), so it can only offer a
 * plugin, an enable switch or a schema-driven parameter the registry and the
 * probe descriptor allow. Until the native probe lands (P1 second half) the
 * rows cannot carry a descriptor and importing stays disabled, which the page
 * states explicitly instead of hiding.
 */
@Composable
internal fun PluginSettingsScreen(
    state: GhostlockUiState,
    actions: GhostlockActions,
) {
    val scrollBehavior = MiuixScrollBehavior(rememberTopAppBarState())
    Scaffold(
        topBar = {
            TopAppBar(
                title = stringResource(R.string.plugins),
                scrollBehavior = scrollBehavior,
                navigationIcon = {
                    IconButton(onClick = actions::onClosePlugins) {
                        Icon(
                            imageVector = MiuixIcons.Back,
                            contentDescription = stringResource(R.string.action_back),
                        )
                    }
                },
            )
        },
    ) { paddingValues ->
        LazyColumn(
            modifier = Modifier
                .fillMaxSize()
                .nestedScroll(scrollBehavior.nestedScrollConnection),
            contentPadding = pageContentPadding(paddingValues),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            item(key = "import") {
                Card {
                    if (state.pluginImportEnabled) {
                        ArrowPreference(
                            title = stringResource(R.string.plugin_import),
                            summary = stringResource(R.string.plugin_import_summary),
                            onClick = actions::onImportPlugin,
                        )
                    } else {
                        Column {
                            SmallTitle(text = stringResource(R.string.plugin_import))
                            Text(
                                text = stringResource(R.string.plugin_import_blocked),
                                modifier = Modifier.padding(
                                    start = 16.dp,
                                    end = 16.dp,
                                    top = 4.dp,
                                    bottom = 12.dp,
                                ),
                                style = MiuixTheme.textStyles.body2,
                                color = MiuixTheme.colorScheme.onSurfaceVariantSummary,
                            )
                        }
                    }
                }
            }
            if (state.pluginRows.isEmpty()) {
                item(key = "empty") {
                    Card {
                        Text(
                            text = stringResource(R.string.plugins_empty),
                            modifier = Modifier.padding(16.dp),
                            style = MiuixTheme.textStyles.body2,
                            color = MiuixTheme.colorScheme.onSurfaceVariantSummary,
                        )
                    }
                }
            }
            items(state.pluginRows, key = { row -> row.id }) { row ->
                Card {
                    Column {
                        SwitchPreference(
                            checked = row.enabled,
                            onCheckedChange = { enabled ->
                                actions.onPluginEnabledChanged(row.id, enabled)
                            },
                            title = row.id + " " + row.version,
                            summary = row.summary + " · " + row.sha256Short,
                            enabled = row.selectable,
                        )
                        val blocked = row.blockedReason
                        if (blocked != null) {
                            Text(
                                text = stringResource(R.string.plugin_blocked, blocked),
                                modifier = Modifier.padding(
                                    start = 16.dp,
                                    end = 16.dp,
                                    top = 4.dp,
                                    bottom = 12.dp,
                                ),
                                style = MiuixTheme.textStyles.body2,
                                color = MiuixTheme.colorScheme.onSurfaceVariantSummary,
                            )
                        }
                        /* The parameter schema the module declares: rendered from
                         * the descriptor, so the page never invents a key. Editing
                         * lands with the wire emission (P1 batch B). */
                        val params = state.pluginParams[row.id].orEmpty()
                        if (params.isNotEmpty()) {
                            SmallTitle(text = stringResource(R.string.plugin_params_title))
                            for (param in params) {
                                if (param.editor == PluginParamEditor.Switch) {
                                    /* Bool parameters are switches: there is no
                                     * text spelling to validate or to mistype. */
                                    SwitchPreference(
                                        checked = param.value.equals("true", ignoreCase = true),
                                        onCheckedChange = { checked ->
                                            actions.onPluginBoolChanged(
                                                row.id,
                                                param.name,
                                                checked,
                                            )
                                        },
                                        title = param.name,
                                        summary = pluginParamSummary(param),
                                    )
                                } else {
                                    ArrowPreference(
                                        title = param.name,
                                        summary = pluginParamSummary(param),
                                        onClick = {
                                            actions.onPluginParamEdit(
                                                row.id,
                                                param.name,
                                                param.defaultText ?: param.value,
                                            )
                                        },
                                    )
                                }
                            }
                            Spacer(modifier = Modifier.height(8.dp))
                        }
                    }
                }
            }
        }
    }
}
