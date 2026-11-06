package com.ghostlock.app.domain.payload

/**
 * Typed version-three Payload persistence document.
 *
 * @property version Must equal [PAYLOAD_DOCUMENT_VERSION].
 * @property definition Backend-neutral Payload authority.
 */
data class PayloadDocumentV3(
    val version: Int = PAYLOAD_DOCUMENT_VERSION,
    val definition: PayloadDefinition,
)

/** Sole accepted and emitted Payload document version. */
const val PAYLOAD_DOCUMENT_VERSION: Int = 3
