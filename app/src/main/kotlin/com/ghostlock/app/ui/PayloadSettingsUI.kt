package com.ghostlock.app.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.unit.dp
import com.ghostlock.app.R
import top.yukonga.miuix.kmp.basic.BasicComponent
import top.yukonga.miuix.kmp.basic.BasicComponentDefaults
import top.yukonga.miuix.kmp.basic.Card
import top.yukonga.miuix.kmp.basic.HorizontalDivider
import top.yukonga.miuix.kmp.basic.Icon
import top.yukonga.miuix.kmp.basic.IconButton
import top.yukonga.miuix.kmp.basic.MiuixScrollBehavior
import top.yukonga.miuix.kmp.basic.Scaffold
import top.yukonga.miuix.kmp.basic.SmallTitle
import top.yukonga.miuix.kmp.basic.SmallTopAppBar
import top.yukonga.miuix.kmp.basic.Text
import top.yukonga.miuix.kmp.basic.TextField
import top.yukonga.miuix.kmp.basic.rememberTopAppBarState
import top.yukonga.miuix.kmp.icon.MiuixIcons
import top.yukonga.miuix.kmp.icon.extended.Back
import top.yukonga.miuix.kmp.preference.ArrowPreference
import top.yukonga.miuix.kmp.theme.MiuixTheme
import top.yukonga.miuix.kmp.utils.overScrollVertical
import top.yukonga.miuix.kmp.utils.scrollEndHaptic

/**
 * The custom-execution page.
 *
 * ONE radio list of four tiers; the chosen tier's own controls open INSIDE that
 * row's card (divider + inset), so the owner of every control is unambiguous.
 *
 * There is deliberately no check list, no confirmation step and no clear button:
 * a blocker is stated next to the run button (and when run is tapped), switching
 * to "default" IS clearing, and the run summary line already says what this run
 * will execute.
 *
 * Layout follows the About page (the reference rhythm): `SmallTopAppBar`, the
 * `scrollEndHaptic` + `overScrollVertical` + `nestedScroll` chain, one `Card`
 * per row with the same `horizontal 12 / bottom 12` box, and a closing spacer.
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
    /* Which managers exist AND can be opened. The check is the same one the
     * launch performs, so a row can never offer a target that cannot start. */
    val context = LocalContext.current
    val installed = remember {
        RootManager.entries
            .filter { context.packageManager.getLaunchIntentForPackage(it.packageName) != null }
            .map { it.packageName }
            .toSet()
    }
    val managerRows = payloadManagerRows(draft, installed)
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
                    PayloadTierCard(
                        row = row,
                        draft = draft,
                        managerRows = managerRows,
                        actions = actions,
                    )
                }
            }
            /* The honest note is one quiet line at the very bottom, not a card. */
            item(key = "persistence") {
                Text(
                    text = stringResource(R.string.payload_not_persisted),
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(horizontal = 28.dp, vertical = 16.dp),
                    style = MiuixTheme.textStyles.footnote1,
                    color = MiuixTheme.colorScheme.onSurfaceVariantSummary,
                )
            }
            item(key = "bottom-space") {
                Spacer(Modifier.height(24.dp).navigationBarsPadding())
            }
        }
    }
}

/**
 * One tier row: the title line is always there (the whole row is clickable), and
 * the SELECTED row opens its own controls underneath, inside the same card.
 *
 * The card surface is the ORDINARY card colour: the selected state is carried by
 * the indicator and the title colour, exactly like the app's other radio lists —
 * not by a grey container (the user rejected the grey highlight).
 */
@Composable
private fun PayloadTierCard(
    row: PayloadTierRow,
    draft: PayloadDraft,
    managerRows: List<RootManagerRow>,
    actions: GhostlockActions,
) {
    Card(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 12.dp)
            .padding(bottom = 12.dp),
    ) {
        Column {
            RadioRow(
                title = stringResource(row.tier.titleRes()),
                summary = stringResource(row.tier.summaryRes()),
                selected = row.selected,
                onClick = { actions.onPayloadTierChanged(row.tier) },
            )
            if (!row.expanded) return@Column
            /* The default tier chooses which root manager takes over: the system
             * default first, then every manager that is actually installed. Only
             * INSTALLED managers are offered (the user's rule), so the list can
             * never point at something that is not on the device. */
            if (row.tier == null) {
                HorizontalDivider(modifier = Modifier.padding(start = 16.dp))
                for (managerRow in managerRows) {
                    RadioRow(
                        title = stringResource(
                            managerRow.manager?.labelRes ?: R.string.payload_manager_default,
                        ),
                        summary = if (managerRow.manager == null) {
                            stringResource(R.string.payload_manager_default_summary)
                        } else {
                            managerRow.packageName
                        },
                        selected = managerRow.selected,
                        onClick = { actions.onPayloadManagerChanged(managerRow.manager) },
                    )
                }
                return@Column
            }
            if (row.fieldKeys.isEmpty() && row.actionKeys.isEmpty()) return@Column
            HorizontalDivider(modifier = Modifier.padding(start = 16.dp))
            for (key in row.fieldKeys) {
                when (key) {
                    "exec.command" -> {
                        TextField(
                            value = draft.execCommand,
                            onValueChange = actions::onPayloadCommandChanged,
                            label = stringResource(R.string.payload_exec_command),
                            modifier = Modifier
                                .fillMaxWidth()
                                .padding(start = 16.dp, end = 16.dp, top = 12.dp),
                            singleLine = true,
                        )
                        Text(
                            text = stringResource(R.string.payload_exec_command_hint),
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
                                text = path,
                                modifier = Modifier.padding(
                                    start = 16.dp,
                                    end = 16.dp,
                                    bottom = 12.dp,
                                ),
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
                                modifier = Modifier.padding(
                                    start = 16.dp,
                                    end = 16.dp,
                                    bottom = 12.dp,
                                ),
                                style = MiuixTheme.textStyles.body2,
                                color = MiuixTheme.colorScheme.onSurfaceVariantSummary,
                            )
                        } else {
                            Text(
                                text = stringResource(R.string.payload_ko_order),
                                modifier = Modifier.padding(
                                    start = 16.dp,
                                    end = 16.dp,
                                    bottom = 4.dp,
                                ),
                                style = MiuixTheme.textStyles.body2,
                                color = MiuixTheme.colorScheme.onSurfaceVariantSummary,
                            )
                            /* The number IS the load order. */
                            for ((index, entry) in draft.koEntries.withIndex()) {
                                Text(
                                    text = (index + 1).toString() + ". " + entry.name,
                                    modifier = Modifier.padding(
                                        start = 16.dp,
                                        end = 16.dp,
                                        top = 4.dp,
                                    ),
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

/**
 * One selectable line (a tier, or one root manager in the default tier's
 * submenu). It is the SAME row primitive the app's other radio lists use
 * (`RadioButtonPreference` wraps [BasicComponent]) with the same title and
 * summary styles, the same `primary` colour when selected, and the same radio
 * semantics — only the indicator is drawn by [TierRadioIndicator].
 */
@Composable
private fun RadioRow(title: String, summary: String, selected: Boolean, onClick: () -> Unit) {
    BasicComponent(
        title = title,
        titleColor = if (selected) {
            BasicComponentDefaults.titleColor(color = MiuixTheme.colorScheme.primary)
        } else {
            BasicComponentDefaults.titleColor()
        },
        summary = summary,
        summaryColor = if (selected) {
            BasicComponentDefaults.summaryColor(color = MiuixTheme.colorScheme.primary)
        } else {
            BasicComponentDefaults.summaryColor()
        },
        startAction = { TierRadioIndicator(selected) },
        onClick = onClick,
        role = Role.RadioButton,
        holdDownState = true,
    )
}

/**
 * The radio indicator, so an UNSELECTED row is visibly a choice (the user's
 * complaint: only the selected row showed anything, so the list did not read as
 * one-of-four).
 *
 * EVIDENCE, not taste: miuix 0.9.4's `RadioButton` — what `RadioButtonPreference`
 * uses — draws a checkmark when selected and NOTHING when unselected (its own
 * doc says so), and the app's other radio lists (builtin profiles, combinations)
 * look exactly like that. `RadioButtonPreference` exposes no parameter for an
 * unselected indicator (`RadioButtonColors` has only selected/disabled), so the
 * ring is drawn here, in the ONE place the tier list needs it, using the theme's
 * own colours — `outline` for the empty ring, `primary` for the selected dot —
 * and the same 26.dp footprint as miuix's own indicator.
 */
@Composable
private fun TierRadioIndicator(selected: Boolean) {
    val color = if (selected) {
        MiuixTheme.colorScheme.primary
    } else {
        MiuixTheme.colorScheme.outline
    }
    Canvas(modifier = Modifier.size(26.dp)) {
        val stroke = 2.dp.toPx()
        val radius = (size.minDimension - stroke) / 2f
        drawCircle(color = color, radius = radius, style = Stroke(width = stroke))
        if (selected) drawCircle(color = color, radius = radius * 0.5f)
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
