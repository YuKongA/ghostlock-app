package com.ghostlock.app.ui

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import com.ghostlock.app.R
import top.yukonga.miuix.kmp.basic.Card
import top.yukonga.miuix.kmp.basic.CardDefaults
import top.yukonga.miuix.kmp.basic.HorizontalDivider
import top.yukonga.miuix.kmp.basic.Icon
import top.yukonga.miuix.kmp.basic.IconButton
import top.yukonga.miuix.kmp.basic.MiuixScrollBehavior
import top.yukonga.miuix.kmp.basic.Scaffold
import top.yukonga.miuix.kmp.basic.SmallTitle
import top.yukonga.miuix.kmp.basic.SmallTopAppBar
import top.yukonga.miuix.kmp.basic.Text
import top.yukonga.miuix.kmp.basic.TextButton
import top.yukonga.miuix.kmp.basic.TextField
import top.yukonga.miuix.kmp.basic.rememberTopAppBarState
import top.yukonga.miuix.kmp.icon.MiuixIcons
import top.yukonga.miuix.kmp.icon.extended.Back
import top.yukonga.miuix.kmp.preference.ArrowPreference
import top.yukonga.miuix.kmp.preference.RadioButtonPreference
import top.yukonga.miuix.kmp.theme.MiuixTheme
import top.yukonga.miuix.kmp.utils.overScrollVertical
import top.yukonga.miuix.kmp.utils.scrollEndHaptic

/**
 * payload batch (a): the custom-execution page.
 *
 * Interaction: ONE radio list of four tiers, and the chosen tier's own options
 * expand INSIDE that row's card — never in a separate form area, so the owner of
 * every control is unambiguous. The radio control is the App's existing
 * `RadioButtonPreference` (the same one the builtin-profile and combination
 * pickers use), with the button on the LEFT as the user asked.
 *
 * Layout follows the About page (the reference style): `SmallTopAppBar`, the
 * `rememberLazyListState` + `scrollEndHaptic` + `overScrollVertical` +
 * `nestedScroll` chain, `SmallTitle` as its own item, and one `Card` per row
 * with AboutLink's shape.
 */
@Composable
internal fun PayloadSettingsScreen(
    state: GhostlockUiState,
    actions: GhostlockActions,
) {
    val scrollBehavior = MiuixScrollBehavior(rememberTopAppBarState())
    val listState = rememberLazyListState()
    val draft = state.payloadDraft
    val rows = payloadTierRows(draft)
    val messages = payloadMessages(draft)
    val authorised = !draft.needsAuthorisation || state.payloadConfirmed
    Scaffold(
        topBar = {
            SmallTopAppBar(
                title = stringResource(R.string.payload),
                scrollBehavior = scrollBehavior,
                navigationIcon = {
                    IconButton(onClick = actions::onClosePayload) {
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
            state = listState,
            modifier = Modifier
                .fillMaxSize()
                .scrollEndHaptic()
                .overScrollVertical()
                .nestedScroll(scrollBehavior.nestedScrollConnection),
            contentPadding = pageContentPadding(paddingValues),
        ) {
            item(key = "tier-title") {
                SmallTitle(text = stringResource(R.string.payload_tier))
            }
            for (row in rows) {
                item(key = "tier-" + (row.tier?.token ?: "default")) {
                    PayloadTierCard(row = row, draft = draft, actions = actions)
                }
            }
            if (messages.isNotEmpty()) {
                item(key = "check-title") {
                    SmallTitle(text = stringResource(R.string.payload_report))
                }
                for ((index, message) in messages.withIndex()) {
                    item(key = "issue-" + index) {
                        PayloadNote(
                            text = stringResource(message.resId, *message.args.toTypedArray()),
                            emphasise = message.level == PluginIssueLevel.Error,
                        )
                    }
                }
            }
            if (draft.needsAuthorisation) {
                item(key = "confirm") {
                    PayloadEntry(
                        title = stringResource(R.string.payload_confirm),
                        summary = if (authorised) {
                            stringResource(R.string.payload_confirmed)
                        } else {
                            stringResource(R.string.payload_not_confirmed)
                        },
                        onClick = actions::onPayloadConfirm,
                    )
                }
            }
            /* An ACTION, not a navigation row: a button, no chevron, no summary. */
            item(key = "clear") {
                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(horizontal = 12.dp, vertical = 4.dp),
                    contentAlignment = Alignment.Center,
                ) {
                    TextButton(
                        text = stringResource(R.string.payload_clear),
                        onClick = actions::onPayloadClear,
                    )
                }
            }
            /* The honest note is one quiet line at the very bottom, not a card. */
            item(key = "persistence") {
                Text(
                    text = stringResource(R.string.payload_not_persisted),
                    modifier = Modifier.padding(
                        start = 24.dp,
                        end = 24.dp,
                        top = 4.dp,
                        bottom = 20.dp,
                    ),
                    style = MiuixTheme.textStyles.footnote1,
                    color = MiuixTheme.colorScheme.onSurfaceVariantSummary,
                )
            }
        }
    }
}

/**
 * One radio row: the title line is always there (whole row clickable), and the
 * SELECTED row opens its own controls underneath, inside the same card, marked
 * by a divider and a left inset so the ownership is visible.
 */
@Composable
private fun PayloadTierCard(
    row: PayloadTierRow,
    draft: PayloadDraft,
    actions: GhostlockActions,
) {
    Card(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 12.dp)
            .padding(bottom = 12.dp),
        /* The chosen row is visibly chosen: the radio AND the card surface. */
        colors = if (row.selected) {
            CardDefaults.defaultColors(color = MiuixTheme.colorScheme.secondaryContainer)
        } else {
            CardDefaults.defaultColors()
        },
    ) {
        Column {
            RadioButtonPreference(
                title = stringResource(row.tier.titleRes()),
                summary = stringResource(row.tier.summaryRes()),
                selected = row.selected,
                onClick = { actions.onPayloadTierChanged(row.tier) },
            )
            if (!row.expanded) return@Column
            if (row.fieldKeys.isEmpty() && row.actionKeys.isEmpty()) {
                /* The default tier has nothing to configure. */
                return@Column
            }
            HorizontalDivider(modifier = Modifier.padding(start = 16.dp))
            for (key in row.fieldKeys) {
                val value = when (key) {
                    "exec.command" -> draft.execCommand
                    "exec.sha256" -> draft.execSha256.orEmpty()
                    else -> ""
                }
                TextField(
                    value = value,
                    onValueChange = { text ->
                        if (key == "exec.command") {
                            actions.onPayloadCommandChanged(text)
                        } else {
                            actions.onPayloadHashChanged(text)
                        }
                    },
                    label = stringResource(
                        if (key == "exec.command") {
                            R.string.payload_exec_command
                        } else {
                            R.string.payload_hash
                        },
                    ),
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(start = 16.dp, end = 16.dp, top = 12.dp),
                    singleLine = true,
                )
            }
            if (row.fieldKeys.contains("exec.command")) {
                Text(
                    text = stringResource(R.string.payload_exec_command_hint),
                    modifier = Modifier.padding(start = 16.dp, end = 16.dp, top = 4.dp),
                    style = MiuixTheme.textStyles.body2,
                    color = MiuixTheme.colorScheme.onSurfaceVariantSummary,
                )
            }
            for (key in row.actionKeys) {
                when (key) {
                    "script.pick" -> {
                        ArrowPreference(
                            title = stringResource(R.string.payload_pick_script),
                            summary = draft.scriptName
                                ?: stringResource(R.string.payload_script_none),
                            onClick = actions::onPayloadPickScript,
                        )
                        draft.scriptPath?.let { path ->
                            Text(
                                text = path + " · " +
                                    (draft.scriptSha256 ?: stringResource(R.string.payload_hash_none)),
                                modifier = Modifier.padding(start = 16.dp, end = 16.dp, bottom = 12.dp),
                                style = MiuixTheme.textStyles.body2,
                                color = MiuixTheme.colorScheme.onSurfaceVariantSummary,
                            )
                        }
                    }

                    "ko.pick" -> {
                        ArrowPreference(
                            title = stringResource(R.string.payload_pick_ko),
                            summary = draft.koEntries.size.toString() + " / " + PAYLOAD_MAX_KO,
                            onClick = actions::onPayloadPickKo,
                        )
                        if (draft.koEntries.isEmpty()) {
                            Text(
                                text = stringResource(R.string.payload_ko_none),
                                modifier = Modifier.padding(start = 16.dp, end = 16.dp, bottom = 12.dp),
                                style = MiuixTheme.textStyles.body2,
                                color = MiuixTheme.colorScheme.onSurfaceVariantSummary,
                            )
                        } else {
                            Text(
                                text = stringResource(R.string.payload_ko_order),
                                modifier = Modifier.padding(start = 16.dp, end = 16.dp, bottom = 4.dp),
                                style = MiuixTheme.textStyles.body2,
                                color = MiuixTheme.colorScheme.onSurfaceVariantSummary,
                            )
                            /* The number IS the load order. */
                            for ((index, entry) in draft.koEntries.withIndex()) {
                                Text(
                                    text = (index + 1).toString() + ". " + entry.name,
                                    modifier = Modifier.padding(start = 16.dp, end = 16.dp, top = 4.dp),
                                    style = MiuixTheme.textStyles.body2,
                                    color = MiuixTheme.colorScheme.onSurface,
                                )
                                ArrowPreference(
                                    title = stringResource(R.string.payload_ko_up),
                                    onClick = { actions.onPayloadKoMove(index, -1) },
                                )
                                ArrowPreference(
                                    title = stringResource(R.string.payload_ko_down),
                                    onClick = { actions.onPayloadKoMove(index, 1) },
                                )
                                ArrowPreference(
                                    title = stringResource(R.string.payload_ko_remove),
                                    onClick = { actions.onPayloadKoRemove(index) },
                                )
                            }
                        }
                    }
                }
            }
        }
    }
}

/** The About page's card shape (AboutLink in AboutUI.kt). */
@Composable
private fun PayloadEntry(title: String, summary: String, onClick: () -> Unit) {
    Card(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 12.dp)
            .padding(bottom = 12.dp),
    ) {
        ArrowPreference(title = title, summary = summary, onClick = onClick)
    }
}

/** A text-only card with the same shape as [PayloadEntry]. */
@Composable
private fun PayloadNote(
    text: String,
    emphasise: Boolean = false,
    modifier: Modifier = Modifier,
) {
    Card(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 12.dp)
            .padding(bottom = 12.dp),
    ) {
        Text(
            text = text,
            modifier = modifier.padding(16.dp),
            style = MiuixTheme.textStyles.body2,
            color = if (emphasise) {
                MiuixTheme.colorScheme.primary
            } else {
                MiuixTheme.colorScheme.onSurfaceVariantSummary
            },
        )
    }
}

private fun PayloadTier?.titleRes(): Int = when (this) {
    null -> R.string.payload_tier_default
    PayloadTier.Exec -> R.string.payload_tier_exec
    PayloadTier.Script -> R.string.payload_tier_script
    PayloadTier.Ko -> R.string.payload_tier_ko
}

private fun PayloadTier?.summaryRes(): Int = when (this) {
    null -> R.string.payload_tier_default_summary
    PayloadTier.Exec -> R.string.payload_tier_exec_summary
    PayloadTier.Script -> R.string.payload_tier_script_summary
    PayloadTier.Ko -> R.string.payload_tier_ko_summary
}
