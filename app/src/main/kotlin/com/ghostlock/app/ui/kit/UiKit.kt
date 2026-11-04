package com.ghostlock.app.ui.kit

/**
 * Current semantic renderer supplied from the authoritative `LocalUiMode` at the application root.
 *
 * Pages consume this component surface without owning a second selector or persistence store.
 */
val LocalUiKit = androidx.compose.runtime.staticCompositionLocalOf<GhostlockKit> {
    error("UiKit not provided")
}
