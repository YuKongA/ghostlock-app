package com.ghostlock.app.core.flow

import com.ghostlock.app.domain.run.BackendTerminalHint
import com.ghostlock.app.domain.run.DiagnosticOutcome
import com.ghostlock.app.domain.run.StageResult

/**
 * Backend-owned primitive, dispatch, and cleanup facts returned by one [Flow].
 *
 * @property primitive Root primitive or diagnostic fixture stage.
 * @property scriptDispatch Dispatch of the exact host-published root script.
 * @property cleanup Backend-private safety cleanup.
 * @property terminalHint Backend-held state hint; product terminal remains an application composition.
 * @property diagnosticOutcome Diagnostic-only outcome; product backends must return NOT_APPLICABLE.
 */
data class BackendRunFacts(
    val primitive: StageResult,
    val scriptDispatch: StageResult,
    val cleanup: StageResult,
    val terminalHint: BackendTerminalHint,
    val diagnosticOutcome: DiagnosticOutcome,
)
