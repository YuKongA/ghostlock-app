package com.ghostlock.app.ui.kit

import androidx.compose.ui.graphics.vector.ImageVector

/**
 * Renderer-neutral configuration-row semantics shared by Material and Miuix cards.
 *
 * A caller supplies visible text, current state, and actions once; [GhostlockKit] owns the concrete
 * widgets and grouping geometry for the active appearance.
 */
sealed interface ConfigItem {
    /** Boolean preference with an optional leading semantic icon. */
    data class Toggle(
        val title: String,
        val summary: String?,
        val icon: ImageVector? = null,
        val checked: Boolean,
        val onChange: (Boolean) -> Unit,
    ) : ConfigItem

    /** Single-choice preference rendered with the native selector of each appearance. */
    data class Choice(
        val title: String,
        val summary: String?,
        val items: List<String>,
        val selectedIndex: Int,
        val onSelect: (Int) -> Unit,
    ) : ConfigItem

    /** Action row that opens a picker or another surface. */
    data class Link(
        val title: String,
        val summary: String?,
        val onClick: () -> Unit,
    ) : ConfigItem

    /** Read-only title and summary fact. */
    data class ReadOnly(
        val title: String,
        val summary: String,
    ) : ConfigItem
}
