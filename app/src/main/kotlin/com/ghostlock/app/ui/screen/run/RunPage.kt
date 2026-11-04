package com.ghostlock.app.ui.screen.run

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.WindowInsetsSides
import androidx.compose.foundation.layout.add
import androidx.compose.foundation.layout.displayCutout
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.only
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.systemBars
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.HourglassEmpty
import androidx.compose.material.icons.filled.IosShare
import androidx.compose.material.icons.filled.Pause
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material3.ExtendedFloatingActionButton
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LargeFlexibleTopAppBar
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.material3.rememberTopAppBarState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.snapshotFlow
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.compose.LifecycleResumeEffect
import androidx.lifecycle.viewmodel.compose.viewModel
import com.ghostlock.app.GhostLockApp
import com.ghostlock.app.R
import com.ghostlock.app.ui.LocalUiMode
import com.ghostlock.app.ui.UiMode
import com.ghostlock.app.ui.component.dialog.rememberConfirmDialog
import com.ghostlock.app.ui.component.material.ExpressiveScaffold
import com.ghostlock.app.ui.component.material.expressiveTopAppBarColors
import com.ghostlock.app.ui.viewmodel.RunViewModel
import java.util.Locale
import kotlinx.coroutines.flow.collect
import top.yukonga.miuix.kmp.basic.Button as MiuixButton
import top.yukonga.miuix.kmp.basic.MiuixScrollBehavior
import top.yukonga.miuix.kmp.basic.Scaffold as MiuixScaffold
import top.yukonga.miuix.kmp.basic.Text as MiuixText
import top.yukonga.miuix.kmp.basic.TopAppBar as MiuixTopAppBar
import top.yukonga.miuix.kmp.theme.MiuixTheme

/** Shared terminal background retained from the existing Material and Miuix pages. */
private val TerminalBackground = Color(0xFF101418)

/** Shared run-page terminal/button corner radius. */
private val RunCorner = 12.dp

/**
 * Localized strings resolved once by the renderer shell.
 *
 * @property title Top app-bar title.
 * @property clearLog Clear-terminal accessibility label.
 * @property shareLog Share-terminal accessibility label.
 * @property shareChooser Android chooser title.
 * @property rerunTitle Rerun confirmation title.
 * @property rerunMessage Rerun confirmation body.
 * @property confirm Confirmation label.
 * @property dismiss Dismiss label.
 */
private data class RunLabels(
    val title: String,
    val clearLog: String,
    val shareLog: String,
    val shareChooser: String,
    val rerunTitle: String,
    val rerunMessage: String,
    val confirm: String,
    val dismiss: String,
)

/** State/actions pair acquired from the injected activity-scoped ViewModel. */
private data class RunBinding(
    val state: RunUiState,
    val actions: RunActions,
)

/** Resolves the shared run-page strings inside composition. */
@Composable
private fun runLabels(): RunLabels = RunLabels(
    title = stringResource(R.string.run_page_title),
    clearLog = stringResource(R.string.run_action_clear_log),
    shareLog = stringResource(R.string.run_action_share_log),
    shareChooser = stringResource(R.string.run_share_chooser_title),
    rerunTitle = stringResource(R.string.run_rerun_title),
    rerunMessage = stringResource(R.string.run_rerun_message),
    confirm = stringResource(R.string.confirm),
    dismiss = stringResource(android.R.string.cancel),
)

/** @return Localized primary-button text for the semantic phase. */
@Composable
private fun runLabel(phase: RunUiPhase): String = stringResource(
    when (phase) {
        RunUiPhase.Idle -> R.string.run_state_start
        RunUiPhase.Running -> R.string.run_state_abort
        RunUiPhase.Aborting -> R.string.run_state_aborting
        RunUiPhase.Aborted -> R.string.run_state_aborted
        RunUiPhase.Done,
        RunUiPhase.HandoffRequested -> R.string.run_state_done

        RunUiPhase.RerunReady -> R.string.run_state_rerun
    },
)

/** @return Material primary-button icon for the semantic phase. */
private fun stateIcon(phase: RunUiPhase) = when (phase) {
    RunUiPhase.Idle -> Icons.Filled.PlayArrow
    RunUiPhase.Running -> Icons.Filled.Pause
    RunUiPhase.Aborting -> Icons.Filled.HourglassEmpty
    RunUiPhase.Aborted,
    RunUiPhase.Done,
    RunUiPhase.HandoffRequested -> Icons.Filled.Check

    RunUiPhase.RerunReady -> Icons.Filled.Refresh
}

/** @return Miuix textual icon retained from the existing rounded button. */
private fun stateGlyph(phase: RunUiPhase): String = when (phase) {
    RunUiPhase.Idle -> "▶  "
    RunUiPhase.Running -> "⏸  "
    RunUiPhase.Aborting -> "…  "
    RunUiPhase.Aborted,
    RunUiPhase.Done,
    RunUiPhase.HandoffRequested -> "✓  "

    RunUiPhase.RerunReady -> "↻  "
}

/** @return Existing terminal color mapping shared by both renderers. */
private fun terminalColor(level: RunTerminalLevel): Color = when (level) {
    RunTerminalLevel.Error -> Color(0xFFFF5252)
    RunTerminalLevel.Warn -> Color(0xFFFFD740)
    RunTerminalLevel.Info -> Color(0xFF9BE7A0)
    RunTerminalLevel.Debug -> Color(0xFFFFFFFF)
    RunTerminalLevel.Verbose -> Color(0xFF9E9E9E)
}

/** @return Existing single-character level marker. */
private fun levelMark(level: RunTerminalLevel): Char = when (level) {
    RunTerminalLevel.Verbose -> 'V'
    RunTerminalLevel.Debug -> '~'
    RunTerminalLevel.Info -> '*'
    RunTerminalLevel.Warn -> '-'
    RunTerminalLevel.Error -> '!'
}

/** @return Existing origin bracket and level prefix. */
private fun originLabel(entry: RunTerminalEntry): String {
    val mark = levelMark(entry.level)
    return when (entry.origin) {
        RunTerminalOrigin.Kotlin -> "<$mark> "
        RunTerminalOrigin.Native -> "[$mark] "
        RunTerminalOrigin.Shell -> "($mark) "
    }
}

/** @return Existing seconds.milliseconds elapsed format. */
private fun elapsedText(elapsedMs: Long): String = String.format(
    Locale.US,
    "%02d.%03d",
    elapsedMs / 1000L,
    elapsedMs % 1000L,
)

/**
 * Shared semantic terminal rows; only the final text primitive/style differs by renderer family.
 *
 * @param state Single ViewModel-owned run state.
 * @param modifier Shared scrolling and padding geometry.
 * @param text Renderer-specific text primitive preserving current typography.
 */
@Composable
private fun RunTerminalRows(
    state: RunUiState,
    modifier: Modifier,
    text: @Composable (String, Modifier, Color, FontFamily) -> Unit,
) {
    val font = state.terminalFontFamily
    SelectionContainer {
        Column(
            modifier = modifier,
            verticalArrangement = Arrangement.spacedBy(2.dp),
        ) {
            state.entries.forEach { entry ->
                val prefix = if (state.showTimestamp) {
                    elapsedText(entry.elapsedMs) + " " + originLabel(entry)
                } else {
                    originLabel(entry)
                }
                val color = terminalColor(entry.level)
                Row(modifier = Modifier.fillMaxWidth()) {
                    text(prefix, Modifier, color, font)
                    text(entry.message, Modifier.weight(1f), color, font)
                }
            }
        }
    }
}

/** Hosts the current-theme rerun dialog while the ViewModel owns confirmation state. */
@Composable
private fun RunRerunDialog(
    state: RunUiState,
    actions: RunActions,
    labels: RunLabels,
) {
    val dialog = rememberConfirmDialog(
        onConfirm = actions.onConfirmRerun,
        onDismiss = actions.onDismissRerun,
    )
    LaunchedEffect(state.showRerunConfirmation, labels) {
        if (state.showRerunConfirmation) {
            dialog.showConfirm(
                title = labels.rerunTitle,
                content = labels.rerunMessage,
                confirm = labels.confirm,
                dismiss = labels.dismiss,
            )
        } else if (dialog.isShown) {
            dialog.hide()
        }
    }
}

/**
 * Acquires the single injected state/actions contract used by either page-level renderer branch.
 *
 * @param isCurrentPage Whether the main-pager controller currently selects the run page.
 * @note The pager keeps every main page composed, so controller selection must refresh the
 * authoritative backend gate instead of waiting for the transition to settle.
 */
@Composable
private fun runBinding(isCurrentPage: Boolean): RunBinding {
    val application = LocalContext.current.applicationContext as GhostLockApp
    val runViewModel: RunViewModel = viewModel(factory = application.container.viewModelFactory)
    val state by runViewModel.uiState.collectAsStateWithLifecycle()
    val latestIsCurrentPage by rememberUpdatedState(isCurrentPage)
    val initialResumeHandled = rememberSaveable { mutableStateOf(false) }
    LaunchedEffect(isCurrentPage, runViewModel) {
        if (isCurrentPage) {
            runViewModel.refresh()
        }
    }
    LifecycleResumeEffect(runViewModel) {
        if (initialResumeHandled.value && latestIsCurrentPage) {
            runViewModel.refresh()
        }
        initialResumeHandled.value = true
        onPauseOrDispose { }
    }
    val actions = remember(runViewModel) {
        RunActions(
            onPrimaryAction = runViewModel::onPrimaryAction,
            onClearTerminal = runViewModel::clearTerminal,
            onShareTerminal = runViewModel::shareTerminal,
            onConfirmRerun = runViewModel::confirmRerun,
            onDismissRerun = runViewModel::dismissRerun,
        )
    }
    return RunBinding(state, actions)
}

/**
 * Production entry that binds state/actions once before selecting the page-level appearance.
 *
 * @param bottomInnerPadding Bottom navigation inset supplied by the existing activity shell.
 * @param isCurrentPage Whether the main-pager controller currently selects the run page.
 */
@Composable
fun RunPage(
    bottomInnerPadding: Dp,
    isCurrentPage: Boolean = true,
) {
    val binding = runBinding(isCurrentPage)
    when (LocalUiMode.current) {
        UiMode.Material -> RunPageMaterial(binding.state, binding.actions, bottomInnerPadding)
        UiMode.Miuix -> RunPageMiuix(binding.state, binding.actions, bottomInnerPadding)
    }
}

/**
 * Pure production Material renderer seam used by both the real page and deterministic UI tests.
 *
 * @param state Shared semantic state.
 * @param actions Shared semantic actions.
 * @param bottomInnerPadding Bottom navigation inset supplied by the existing activity shell.
 */
@Composable
internal fun RunPageMaterial(
    state: RunUiState,
    actions: RunActions,
    bottomInnerPadding: Dp,
) {
    val labels = runLabels()
    val scroll = rememberScrollState()
    val scrollBehavior = TopAppBarDefaults.exitUntilCollapsedScrollBehavior(rememberTopAppBarState())
    RunRerunDialog(state, actions, labels)
    LaunchedEffect(scroll) {
        snapshotFlow { scroll.maxValue }.collect { target -> scroll.scrollTo(target) }
    }

    ExpressiveScaffold(
        topBar = {
            LargeFlexibleTopAppBar(
                title = { Text(labels.title) },
                colors = expressiveTopAppBarColors(),
                windowInsets = WindowInsets.safeDrawing.only(
                    WindowInsetsSides.Top + WindowInsetsSides.Horizontal,
                ),
                scrollBehavior = scrollBehavior,
                actions = {
                    IconButton(onClick = actions.onClearTerminal) {
                        Icon(Icons.Filled.Delete, contentDescription = labels.clearLog)
                    }
                    IconButton(onClick = { actions.onShareTerminal(labels.shareChooser) }) {
                        Icon(Icons.Filled.IosShare, contentDescription = labels.shareLog)
                    }
                },
            )
        },
        contentWindowInsets = WindowInsets.safeDrawing.only(
            WindowInsetsSides.Top + WindowInsetsSides.Horizontal,
        ),
    ) { innerPadding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(innerPadding),
        ) {
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .weight(1f)
                    .padding(horizontal = 16.dp)
                    .padding(top = 8.dp, bottom = bottomInnerPadding + 12.dp)
                    .clip(RoundedCornerShape(RunCorner))
                    .background(TerminalBackground),
            ) {
                RunTerminalRows(
                    state = state,
                    modifier = Modifier
                        .fillMaxSize()
                        .verticalScroll(scroll)
                        .padding(12.dp)
                        .padding(bottom = 72.dp),
                ) { value, modifier, color, font ->
                    Text(
                        text = value,
                        modifier = modifier,
                        color = color,
                        fontFamily = font,
                        style = MaterialTheme.typography.bodySmall,
                    )
                }
                ExtendedFloatingActionButton(
                    modifier = Modifier
                        .align(Alignment.BottomEnd)
                        .padding(12.dp),
                    shape = RoundedCornerShape(RunCorner),
                    containerColor = if (state.startBlocked) {
                        MaterialTheme.colorScheme.surfaceVariant
                    } else {
                        MaterialTheme.colorScheme.primaryContainer
                    },
                    contentColor = if (state.startBlocked) {
                        MaterialTheme.colorScheme.onSurfaceVariant
                    } else {
                        MaterialTheme.colorScheme.onPrimaryContainer
                    },
                    onClick = {
                        if (!state.startBlocked) actions.onPrimaryAction()
                    },
                    icon = {
                        Icon(
                            imageVector = stateIcon(state.phase),
                            contentDescription = null,
                        )
                    },
                    text = { Text(runLabel(state.phase)) },
                )
            }
        }
    }
}

/**
 * Pure production Miuix renderer seam using the same semantic state/actions as Material.
 *
 * @param state Shared semantic state.
 * @param actions Shared semantic actions.
 * @param bottomInnerPadding Bottom navigation inset supplied by the existing activity shell.
 */
@Composable
internal fun RunPageMiuix(
    state: RunUiState,
    actions: RunActions,
    bottomInnerPadding: Dp,
) {
    val labels = runLabels()
    val scroll = rememberScrollState()
    val scrollBehavior = MiuixScrollBehavior()
    RunRerunDialog(state, actions, labels)
    LaunchedEffect(scroll) {
        snapshotFlow { scroll.maxValue }.collect { target -> scroll.scrollTo(target) }
    }

    MiuixScaffold(
        topBar = {
            MiuixTopAppBar(
                title = labels.title,
                scrollBehavior = scrollBehavior,
                actions = {
                    IconButton(onClick = actions.onClearTerminal) {
                        Icon(
                            Icons.Filled.Delete,
                            contentDescription = labels.clearLog,
                            tint = MiuixTheme.colorScheme.onBackground,
                        )
                    }
                    IconButton(onClick = { actions.onShareTerminal(labels.shareChooser) }) {
                        Icon(
                            Icons.Filled.IosShare,
                            contentDescription = labels.shareLog,
                            tint = MiuixTheme.colorScheme.onBackground,
                        )
                    }
                },
            )
        },
        contentWindowInsets = WindowInsets.systemBars
            .add(WindowInsets.displayCutout)
            .only(WindowInsetsSides.Horizontal),
    ) { innerPadding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(innerPadding),
        ) {
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .weight(1f)
                    .padding(horizontal = 16.dp)
                    .padding(top = 8.dp, bottom = bottomInnerPadding + 12.dp)
                    .clip(RoundedCornerShape(RunCorner))
                    .background(TerminalBackground),
            ) {
                RunTerminalRows(
                    state = state,
                    modifier = Modifier
                        .fillMaxSize()
                        .verticalScroll(scroll)
                        .padding(12.dp)
                        .padding(bottom = 72.dp),
                ) { value, modifier, color, font ->
                    MiuixText(
                        text = value,
                        modifier = modifier,
                        color = color,
                        fontFamily = font,
                        style = MiuixTheme.textStyles.footnote1,
                    )
                }
                MiuixButton(
                    modifier = Modifier
                        .align(Alignment.BottomEnd)
                        .padding(12.dp)
                        .clip(RoundedCornerShape(RunCorner)),
                    enabled = !state.startBlocked,
                    onClick = actions.onPrimaryAction,
                ) {
                    MiuixText(
                        text = stateGlyph(state.phase) + runLabel(state.phase),
                        style = MiuixTheme.textStyles.button,
                    )
                }
            }
        }
    }
}
