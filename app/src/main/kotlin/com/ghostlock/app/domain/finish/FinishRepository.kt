package com.ghostlock.app.domain.finish

/** Persistence boundary for the canonical version-three Finish document. */
interface FinishRepository {
    /** @return Current Finish definition, or an empty definition when no document exists. */
    fun read(): FinishDefinition

    /**
     * Atomically publishes a canonical version-three Finish document.
     *
     * @param definition Complete definition to persist.
     */
    fun save(definition: FinishDefinition)
}
