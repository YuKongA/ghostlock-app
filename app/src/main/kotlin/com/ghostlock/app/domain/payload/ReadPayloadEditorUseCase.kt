package com.ghostlock.app.domain.payload

/** Reads the canonical Payload definition and adds current manager environment facts. */
class ReadPayloadEditorUseCase(
    private val port: PayloadEditorPort,
) {
    /** @return Complete editor projection for a fresh navigation entry. */
    operator fun invoke(): PayloadEditorState {
        val definition = port.read()
        return PayloadEditorState(
            rootManager = definition.rootManager,
            safeMode = definition.safeMode,
            payloads = definition.items,
            rootManagers = port.rootManagerOptions(),
        )
    }
}
