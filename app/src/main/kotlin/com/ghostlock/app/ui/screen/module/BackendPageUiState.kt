package com.ghostlock.app.ui.screen.module

import androidx.compose.runtime.Immutable

/** One renderer-neutral backend selector row. */
@Immutable
data class BackendOptionUiState(
    val key: String,
    val title: String,
    val selectable: Boolean,
)

/** Shared semantic state for the Material and Miuix path-page renderers. */
@Immutable
data class BackendPageUiState(
    val options: List<BackendOptionUiState>,
    val selectedKey: String,
) {
    /** @return Current selected index with a deterministic first-entry fallback. */
    val selectedIndex: Int
        get() = options.indexOfFirst { it.key == selectedKey }.takeIf { it >= 0 } ?: 0
}

/** Shared path-page actions; skin renderers do not own persistence or global page state. */
@Immutable
data class BackendPageActions(
    val onSelectBackend: (String) -> Unit,
    val onOpenPayload: () -> Unit,
    val onOpenFinishSettings: () -> Unit,
)
