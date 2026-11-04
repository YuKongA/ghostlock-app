package com.ghostlock.app.domain.finish

/** Domain boundary for finish payload editing and script preview generation. */
interface FinishEditorPort {
    /** @return Current persisted editor projection and manager install facts. */
    fun read(): FinishEditorState

    /**
     * Persists editor-owned fields while preserving settings and handoff-only fields.
     *
     * @param state Complete editor projection to persist.
     */
    fun save(state: FinishEditorState)

    /**
     * Generates the existing finish preview from an editor projection.
     *
     * @param state Editor projection used for the preview.
     * @return Absolute preview-file path.
     */
    fun preview(state: FinishEditorState): String
}
