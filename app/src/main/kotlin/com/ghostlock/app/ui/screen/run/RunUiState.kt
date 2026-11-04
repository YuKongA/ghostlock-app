package com.ghostlock.app.ui.screen.run

import androidx.compose.runtime.Immutable
import androidx.compose.ui.text.font.FontFamily

/** Renderer-facing run lifecycle, including the page-local rerun-ready interaction state. */
enum class RunUiPhase {
    Idle,
    Running,
    Aborting,
    Aborted,
    Done,
    RerunReady,
    HandoffRequested,
}

/** Visual terminal severity shared by Material and Miuix renderers. */
enum class RunTerminalLevel {
    Verbose,
    Debug,
    Info,
    Warn,
    Error,
}

/** Visual terminal origin shared by Material and Miuix renderers. */
enum class RunTerminalOrigin {
    Kotlin,
    Native,
    Shell,
}

/**
 * One renderer-neutral terminal row.
 *
 * @property origin Stable origin used to render the prefix.
 * @property level Stable severity used to render prefix and body colors.
 * @property message Literal body text.
 * @property elapsedMs Non-negative elapsed run time.
 */
@Immutable
data class RunTerminalEntry(
    val origin: RunTerminalOrigin,
    val level: RunTerminalLevel,
    val message: String,
    val elapsedMs: Long = 0L,
)

/**
 * Single semantic state consumed by both production run-page renderers.
 *
 * @property backendKey Stable backend key used only by ViewModel actions.
 * @property backendLabel Visible backend identity captured at run start.
 * @property startAllowed Authoritative gate outcome for Idle/RerunReady start actions.
 * @property blockedReasons Localized gate facts available to tests and future shared affordances.
 * @property abortable Whether a running primary action may request launcher cancellation.
 * @property showTimestamp Whether terminal prefixes include elapsed time.
 * @property terminalFontFamily Resolved terminal font consumed without renderer-side file access.
 * @property phase Current semantic page phase.
 * @property entries Immutable terminal rows.
 * @property showRerunConfirmation Whether the renderer should show its current-theme confirmation.
 */
@Immutable
data class RunUiState(
    val backendKey: String? = null,
    val backendLabel: String = "-",
    val startAllowed: Boolean = false,
    val blockedReasons: List<String> = emptyList(),
    val abortable: Boolean = false,
    val showTimestamp: Boolean = false,
    val terminalFontFamily: FontFamily = FontFamily.Monospace,
    val phase: RunUiPhase = RunUiPhase.Idle,
    val entries: List<RunTerminalEntry> = emptyList(),
    val showRerunConfirmation: Boolean = false,
) {
    /** @return True only when a start/rerun button must be visually and interactively disabled. */
    val startBlocked: Boolean
        get() = !startAllowed && (phase == RunUiPhase.Idle || phase == RunUiPhase.RerunReady)
}
