package com.ghostlock.app.domain.finish

/** Persists editor-owned finish fields through the domain boundary. */
class SaveFinishEditorUseCase(
    private val port: FinishEditorPort,
) {
    /** @param state Complete editor projection to persist. */
    operator fun invoke(state: FinishEditorState) {
        port.save(state)
    }
}
