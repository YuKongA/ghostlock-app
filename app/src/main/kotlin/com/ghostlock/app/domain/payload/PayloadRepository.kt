package com.ghostlock.app.domain.payload

/** Persistence boundary for the canonical version-three Payload document. */
interface PayloadRepository {
    /** @return Current Payload definition, or the deterministic default when no document exists. */
    fun read(): PayloadDefinition

    /**
     * Atomically publishes a canonical version-three Payload document.
     *
     * @param definition Complete definition to persist.
     * @throws IllegalArgumentException When the definition violates the document contract.
     * @throws IllegalStateException When publication fails.
     */
    fun save(definition: PayloadDefinition)
}
