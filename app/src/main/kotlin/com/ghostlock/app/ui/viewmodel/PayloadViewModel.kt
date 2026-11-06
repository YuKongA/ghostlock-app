package com.ghostlock.app.ui.viewmodel

import androidx.lifecycle.ViewModel
import com.ghostlock.app.domain.payload.PayloadEditorState
import com.ghostlock.app.domain.payload.PayloadKind
import com.ghostlock.app.domain.payload.PayloadSlot
import com.ghostlock.app.domain.payload.ReadPayloadEditorUseCase
import com.ghostlock.app.domain.payload.SavePayloadEditorUseCase
import com.ghostlock.app.domain.payload.newPayloadSlot
import com.ghostlock.app.domain.run.PreviewRootScriptUseCase
import com.ghostlock.app.ui.screen.module.payload.PayloadUiState
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/**
 * Owns Payload editing while persistence and preview remain independent domain operations.
 *
 * @param readPayloadEditor Reads the canonical Payload definition plus environment facts.
 * @param savePayloadEditor Persists only the version-three Payload document.
 * @param previewRootScript Renders Payload and Finish through the production renderer in an isolated workspace.
 */
class PayloadViewModel(
    private val readPayloadEditor: ReadPayloadEditorUseCase,
    private val savePayloadEditor: SavePayloadEditorUseCase,
    private val previewRootScript: PreviewRootScriptUseCase,
) : ViewModel() {
    private val mutableUiState = MutableStateFlow(PayloadUiState(readPayloadEditor()))
    val uiState: StateFlow<PayloadUiState> = mutableUiState.asStateFlow()

    /** Reloads persisted values without changing transient editor controls. */
    fun refresh() {
        mutableUiState.value = mutableUiState.value.copy(editor = readPayloadEditor())
    }

    /** Reloads the Payload authority and resets page-local edit/dialog state. */
    fun openPayload() {
        mutableUiState.value = PayloadUiState(editor = readPayloadEditor())
    }

    /** Toggles list editing without changing persisted Payload data. */
    fun toggleEditMode() {
        mutableUiState.value = mutableUiState.value.copy(
            editMode = !mutableUiState.value.editMode,
            showAddDialog = false,
        )
    }

    /** Controls the add-Payload dialog as transient presentation state. */
    fun setAddDialogVisible(visible: Boolean) {
        mutableUiState.value = mutableUiState.value.copy(showAddDialog = visible)
    }

    /** Replaces one Payload item by stable identity. */
    fun updatePayload(slot: PayloadSlot) = updateEditor { current ->
        current.copy(payloads = current.payloads.map { if (it.id == slot.id) slot else it })
    }

    /** Removes one Payload item by stable identity. */
    fun deletePayload(id: String) = updateEditor { current ->
        current.copy(payloads = current.payloads.filterNot { it.id == id })
    }

    /** Persists a new order only when the caller supplies the same identity set. */
    fun movePayloads(payloads: List<PayloadSlot>) {
        val current = mutableUiState.value.editor.payloads
        if (payloads.map { it.id }.toSet() != current.map { it.id }.toSet()) return
        updateEditor { it.copy(payloads = payloads) }
    }

    /** Appends a session-unique Payload item. */
    fun addPayload(kind: PayloadKind) = updateEditor { current ->
        current.copy(payloads = current.payloads + newPayloadSlot(kind))
    }

    /** Selects an installed manager option by its stable name. */
    fun setRootManager(name: String) {
        if (mutableUiState.value.editor.rootManagers.none { it.name == name && it.installed }) return
        updateEditor { it.copy(rootManager = name) }
    }

    /** Persists manager safe mode without altering ordered items. */
    fun setSafeMode(enabled: Boolean) = updateEditor { it.copy(safeMode = enabled) }

    /**
     * Publishes a non-executable preview through the same renderer used by production staging.
     *
     * @return Success with the absolute isolated preview path, or a captured validation/publication failure.
     * @note Preview failure is presentation data and must never escape into the Android main loop.
     */
    fun savePreview(): Result<String> {
        val editor = mutableUiState.value.editor
        if (editor.hasIncompletePayload) {
            return Result.failure(IllegalStateException("enabled Payload item is incomplete"))
        }
        return runCatching { previewRootScript() }
    }

    /** Persists one immutable editor transform and publishes the same projection. */
    private fun updateEditor(transform: (PayloadEditorState) -> PayloadEditorState) {
        val next = transform(mutableUiState.value.editor)
        savePayloadEditor(next)
        mutableUiState.value = mutableUiState.value.copy(editor = next)
    }
}
