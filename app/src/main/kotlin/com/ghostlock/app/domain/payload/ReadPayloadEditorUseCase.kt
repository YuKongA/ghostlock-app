package com.ghostlock.app.domain.payload

/** Reads the canonical Payload definition and adds current manager environment facts. */
class ReadPayloadEditorUseCase(
    private val port: PayloadEditorPort,
) {
    /** @return Deterministic missing-store editor plus current non-persisted manager facts. */
    fun defaultState(): PayloadEditorState = PayloadDefinition().toEditorState()

    /** @return Complete editor projection for a fresh navigation entry. */
    suspend operator fun invoke(): PayloadEditorState {
        val definition = port.snapshot().await()
        return definition.toEditorState()
    }

    /** @return Editor projection with live manager facts attached. */
    private fun PayloadDefinition.toEditorState(): PayloadEditorState = PayloadEditorState(
        rootManager = rootManager,
        safeMode = safeMode,
        payloads = items,
        rootManagers = port.rootManagerOptions(),
    )
}
