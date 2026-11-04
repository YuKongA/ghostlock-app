package com.ghostlock.app.domain.finish

/** Reads the complete finish-editor projection. */
class ReadFinishEditorUseCase(
    private val port: FinishEditorPort,
) {
    /** @return Current editable finish state and manager environment facts. */
    operator fun invoke(): FinishEditorState = port.read()
}
