package com.ghostlock.app.domain.finish

/**
 * Typed version-three Finish persistence document.
 *
 * @property version Must equal [FINISH_DOCUMENT_VERSION].
 * @property definition Backend-neutral Finish authority.
 */
data class FinishDocumentV3(
    val version: Int = FINISH_DOCUMENT_VERSION,
    val definition: FinishDefinition,
)

/** Sole accepted and emitted Finish document version. */
const val FINISH_DOCUMENT_VERSION: Int = 3
