package com.ghostlock.app.ui.screen.run

import androidx.compose.runtime.Immutable

/**
 * Shared semantic action contract consumed by both production run-page renderers.
 *
 * @property onPrimaryAction Start, abort, or rerun transition selected from current ViewModel state.
 * @property onClearTerminal Clears retained rows without cancelling execution.
 * @property onShareTerminal Exports current rows with the renderer-resolved chooser title.
 * @property onConfirmRerun Confirms the pending repeated execution.
 * @property onDismissRerun Dismisses the pending repeated execution without changing the session.
 */
@Immutable
data class RunActions(
    val onPrimaryAction: () -> Unit,
    val onClearTerminal: () -> Unit,
    val onShareTerminal: (String) -> Unit,
    val onConfirmRerun: () -> Unit,
    val onDismissRerun: () -> Unit,
)
