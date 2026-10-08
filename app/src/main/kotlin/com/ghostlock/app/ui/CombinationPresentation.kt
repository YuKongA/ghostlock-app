package com.ghostlock.app.ui

import androidx.compose.runtime.Composable
import androidx.compose.ui.res.stringResource
import com.ghostlock.app.R
import com.ghostlock.app.data.component.stepNames

import com.ghostlock.app.data.component.CombinationCatalog
import com.ghostlock.app.data.component.CombinationSpec

/**
 * S4 R6b / F4 dropdown presentation, derived from the native-exported
 * combination manifest (see [CombinationCatalog]).
 *
 * The selector rows and the summary text are pure projections of the loaded
 * catalogue, so the UI cannot offer a token or an availability the manifest does
 * not declare; CombinationTokenAgreementTest asserts these projections against
 * the exported resource (test copy vs runtime copy vs dropdown presentation).
 */
data class CombinationOption(
    /** The catalogue row this dropdown entry presents. */
    val spec: CombinationSpec,
    /** Selectable rows only; a planned row is shown disabled. */
    val enabled: Boolean,
    /** True for a planned (available = false) row: labelled, not selectable. */
    val planned: Boolean,
)

/** The single dropdown's rows, catalogue order. */
fun combinationOptions(): List<CombinationOption> = CombinationCatalog.specs.map {
    CombinationOption(spec = it, enabled = it.available, planned = !it.available)
}

/** Backend + route + step set + terminal summary the manifest exports per row. */
/**
 * Localised one-line summary: the DECLARED step sequence then the terminal,
 * both from existing vocabulary (never the native English doc string). Steps
 * come from the exported stepset manifest via stepNames(); a missing row makes
 * that call fail hard, so the fallback shows the token instead of crashing --
 * the sub-page still reports the failure as an error row (fail-visible).
 */
@Composable
fun combinationSummary(spec: CombinationSpec): String {
    val steps = try {
        spec.steps.stepNames().joinToString(" > ")
    } catch (error: IllegalArgumentException) {
        spec.token
    }
    return stringResource(R.string.combination_summary_format, steps, spec.terminal.name)
}
