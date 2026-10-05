package com.ghostlock.app.ui

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
fun combinationSummary(spec: CombinationSpec): String = spec.doc
