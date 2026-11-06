package com.ghostlock.app.domain.payload

/** Persists an editor projection through the sole Payload repository authority. */
class SavePayloadEditorUseCase(
    private val port: PayloadEditorPort,
) {
    /** @param state Complete editor projection; environment facts are not persisted. */
    operator fun invoke(state: PayloadEditorState) {
        port.save(state.toDefinition())
    }
}
