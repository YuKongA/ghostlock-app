package com.ghostlock.app.domain.payload

import kotlinx.coroutines.Deferred

/** Persists an editor projection through the sole Payload repository authority. */
class SavePayloadEditorUseCase(
    private val port: PayloadEditorPort,
) {
    /** @param state Complete editor projection; environment facts are not persisted. */
    operator fun invoke(state: PayloadEditorState): Deferred<PayloadDefinition> =
        port.submit { state.toDefinition() }

    /** Submits a stable-identity transform without replacing unrelated actor-current fields. */
    fun transform(
        mutation: (PayloadDefinition) -> PayloadDefinition,
    ): Deferred<PayloadDefinition> = port.submit(mutation)
}
