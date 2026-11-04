package com.ghostlock.app.domain.finish

/** Generates a finish-script preview through the existing outer adapter. */
class PreviewFinishScriptUseCase(
    private val port: FinishEditorPort,
) {
    /**
     * @param state Current editor projection.
     * @return Absolute path of the generated preview.
     */
    operator fun invoke(state: FinishEditorState): String = port.preview(state)
}
