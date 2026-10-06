package com.ghostlock.app.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.ghostlock.app.R
import top.yukonga.miuix.kmp.basic.Card
import top.yukonga.miuix.kmp.basic.Icon
import top.yukonga.miuix.kmp.basic.IconButton
import top.yukonga.miuix.kmp.basic.MiuixScrollBehavior
import top.yukonga.miuix.kmp.basic.Scaffold
import top.yukonga.miuix.kmp.basic.SmallTitle
import top.yukonga.miuix.kmp.basic.Text
import top.yukonga.miuix.kmp.basic.SmallTopAppBar
import top.yukonga.miuix.kmp.basic.rememberTopAppBarState
import top.yukonga.miuix.kmp.icon.MiuixIcons
import top.yukonga.miuix.kmp.icon.extended.Back
import top.yukonga.miuix.kmp.preference.ArrowPreference
import top.yukonga.miuix.kmp.preference.SwitchPreference
import top.yukonga.miuix.kmp.theme.MiuixTheme
import top.yukonga.miuix.kmp.utils.overScrollVertical
import top.yukonga.miuix.kmp.utils.scrollEndHaptic

/**
 * batch ②: one plugin's page — identity and integrity, the check report, the
 * typed parameters and the READ-ONLY extractor values.
 *
 * Everything here is a projection of [GhostlockUiState.pluginDetail]; the page
 * cannot invent a field, and extract declarations/values are never editable
 * (they are the extractor's output, not user configuration).
 */
@Composable
internal fun PluginDetailScreen(
    state: GhostlockUiState,
    actions: GhostlockActions,
) {
    val scrollBehavior = MiuixScrollBehavior(rememberTopAppBarState())
    val listState = rememberLazyListState()
    val detail = state.pluginDetail
    /* The words the pure projections spell out, resolved once per screen. */
    val texts = PluginTexts(
        required = stringResource(R.string.plugin_text_required),
        unresolved = stringResource(R.string.plugin_text_unresolved),
        defaultLadder = stringResource(R.string.plugin_text_default_ladder),
    )
    Scaffold(
        topBar = {
            SmallTopAppBar(
                title = detail?.id ?: stringResource(R.string.plugin_detail_title),
                scrollBehavior = scrollBehavior,
                navigationIcon = {
                    IconButton(onClick = actions::onClosePluginDetail) {
                        Icon(
                            imageVector = MiuixIcons.Back,
                            contentDescription = stringResource(R.string.action_back),
                        )
                    }
                },
            )
        },
    ) { paddingValues ->
        if (detail == null) return@Scaffold
        LazyColumn(
            state = listState,
            modifier = Modifier
                .fillMaxSize()
                .scrollEndHaptic()
                .overScrollVertical()
                .nestedScroll(scrollBehavior.nestedScrollConnection),
            contentPadding = pageContentPadding(paddingValues),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            item(key = "header-title") {
                SmallTitle(text = stringResource(R.string.plugin_detail_header))
            }
            item(key = "header") {
                Card {
                    Column {
                        for (row in detail.header) {
                            Row(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .padding(horizontal = 16.dp, vertical = 2.dp),
                            ) {
                                Text(
                                    text = stringResource(row.labelRes),
                                    modifier = Modifier.padding(end = 12.dp),
                                    style = MiuixTheme.textStyles.body2,
                                    color = MiuixTheme.colorScheme.onSurfaceVariantSummary,
                                )
                                /* The digest must be copyable in full: it is what
                                 * the registry pins and what an external check
                                 * compares against. */
                                SelectionContainer(modifier = Modifier.fillMaxWidth()) {
                                    Text(
                                        text = pluginHeaderValueText(row.value),
                                        style = MiuixTheme.textStyles.body2,
                                        color = MiuixTheme.colorScheme.onSurface,
                                        overflow = TextOverflow.Ellipsis,
                                        maxLines = 2,
                                    )
                                }
                            }
                        }
                    }
                }
            }
            item(key = "actions") {
                Card {
                    Column {
                        ArrowPreference(
                            title = stringResource(R.string.plugin_detail_recheck),
                            summary = stringResource(R.string.plugin_detail_recheck_summary),
                            onClick = { actions.onRecheckPlugin(detail.id) },
                        )
                        ArrowPreference(
                            title = stringResource(R.string.plugin_detail_clear),
                            summary = stringResource(R.string.plugin_detail_clear_summary),
                            onClick = { actions.onClearPluginOverrides(detail.id) },
                        )
                    }
                }
            }
            item(key = "report-title") {
                SmallTitle(text = stringResource(R.string.plugin_detail_report))
            }
            item(key = "report") {
                Card {
                    Column {
                        if (detail.issues.isEmpty()) {
                            Text(
                                text = stringResource(R.string.plugin_detail_report_clean),
                                modifier = Modifier.padding(16.dp),
                                style = MiuixTheme.textStyles.body2,
                                color = MiuixTheme.colorScheme.onSurfaceVariantSummary,
                            )
                        }
                        for (issue in detail.issues) {
                            Text(
                                text = pluginLineText(PluginLine(issue.resId, issue.args)),
                                modifier = Modifier.padding(
                                    start = 16.dp,
                                    end = 16.dp,
                                    top = 2.dp,
                                    bottom = 2.dp,
                                ),
                                style = MiuixTheme.textStyles.body2,
                                color = if (issue.level == PluginIssueLevel.Error) {
                                    MiuixTheme.colorScheme.primary
                                } else {
                                    MiuixTheme.colorScheme.onSurfaceVariantSummary
                                },
                            )
                        }
                    }
                }
            }
            if (detail.params.isNotEmpty()) {
                item(key = "params-title") {
                    SmallTitle(text = stringResource(R.string.plugin_params_title))
                }
                item(key = "params") {
                    Card {
                        Column {
                            for (param in detail.params) {
                                if (param.editor == PluginParamEditor.Switch) {
                                    SwitchPreference(
                                        checked = param.value.equals("true", ignoreCase = true),
                                        onCheckedChange = { checked ->
                                            actions.onPluginBoolChanged(
                                                detail.id,
                                                param.name,
                                                checked,
                                            )
                                        },
                                        title = param.name,
                                        summary = pluginParamSummary(param, texts),
                                    )
                                } else {
                                    ArrowPreference(
                                        title = param.name,
                                        summary = pluginParamSummary(param, texts),
                                        onClick = {
                                            actions.onPluginParamEdit(
                                                detail.id,
                                                param.name,
                                                param.defaultText ?: param.value,
                                            )
                                        },
                                    )
                                }
                            }
                        }
                    }
                }
            }
            /* P2: what the plugin needs the extractor to resolve, and how it
             * asked for it. READ-ONLY by design: the declaration is the plugin's
             * (a user-typed extraction would bypass the extractor's authority). */
            if (detail.specs.isNotEmpty()) {
                item(key = "specs-title") {
                    SmallTitle(text = stringResource(R.string.plugin_detail_specs))
                }
                item(key = "specs") {
                    Card {
                        Column {
                            for (spec in detail.specs) {
                                Text(
                                    text = pluginSpecSummary(spec, texts),
                                    modifier = Modifier.padding(
                                        start = 16.dp,
                                        end = 16.dp,
                                        top = 2.dp,
                                        bottom = 2.dp,
                                    ),
                                    style = MiuixTheme.textStyles.body2,
                                    color = if (spec.required && spec.value == null) {
                                        MiuixTheme.colorScheme.primary
                                    } else {
                                        MiuixTheme.colorScheme.onSurfaceVariantSummary
                                    },
                                )
                            }
                        }
                    }
                }
            }
            /* Extract declarations and their values are the extractor's, so the
             * page shows them and offers no editing control at all. */
            if (detail.extracts.isNotEmpty()) {
                item(key = "extracts-title") {
                    SmallTitle(text = stringResource(R.string.plugin_detail_extracts))
                }
                item(key = "extracts") {
                    Card {
                        Column {
                            for (extract in detail.extracts) {
                                Text(
                                    text = pluginExtractSummary(extract, texts),
                                    modifier = Modifier.padding(
                                        start = 16.dp,
                                        end = 16.dp,
                                        top = 2.dp,
                                        bottom = 2.dp,
                                    ),
                                    style = MiuixTheme.textStyles.body2,
                                    color = MiuixTheme.colorScheme.onSurfaceVariantSummary,
                                )
                            }
                        }
                    }
                }
            }
        }
    }
}
