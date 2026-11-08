package com.ghostlock.app.ui.viewmodel

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.ghostlock.app.domain.payload.PayloadDefinition
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
import kotlinx.coroutines.Deferred
import kotlinx.coroutines.launch

/**
 * Owns Payload editing while persistence and preview remain independent domain operations.
 *
 * @param readPayloadEditor Reads the authoritative Payload aggregate plus environment facts.
 * @param savePayloadEditor Persists only the complete Payload aggregate.
 * @param previewRootScript Renders Payload with an explicit empty Finish in an isolated workspace.
 */
class PayloadViewModel(
    private val readPayloadEditor: ReadPayloadEditorUseCase,
    private val savePayloadEditor: SavePayloadEditorUseCase,
    private val previewRootScript: PreviewRootScriptUseCase,
) : ViewModel() {
    private val mutableUiState = MutableStateFlow(PayloadUiState(readPayloadEditor.defaultState()))
    val uiState: StateFlow<PayloadUiState> = mutableUiState.asStateFlow()

    /** Reloads persisted values without changing transient editor controls. */
    fun refresh() {
        viewModelScope.launch {
            runCatching { readPayloadEditor() }
                .onSuccess { editor ->
                    mutableUiState.value = mutableUiState.value.copy(editor = editor)
                }
        }
    }

    /** Reloads the Payload authority and resets page-local edit/dialog state. */
    fun openPayload() {
        viewModelScope.launch {
            runCatching { readPayloadEditor() }
                .onSuccess { editor -> mutableUiState.value = PayloadUiState(editor = editor) }
        }
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
    fun updatePayload(slot: PayloadSlot) = updateEditor(
        local = { current ->
            current.copy(payloads = current.payloads.map { if (it.id == slot.id) slot else it })
        },
        persisted = { current ->
            current.copy(items = current.items.map { if (it.id == slot.id) slot else it })
        },
    )

    /** Removes one Payload item by stable identity. */
    fun deletePayload(id: String) = updateEditor(
        local = { current -> current.copy(payloads = current.payloads.filterNot { it.id == id }) },
        persisted = { current -> current.copy(items = current.items.filterNot { it.id == id }) },
    )

    /** Persists a new order only when the caller supplies the same identity set. */
    fun movePayloads(payloads: List<PayloadSlot>) {
        val current = mutableUiState.value.editor.payloads
        if (payloads.map { it.id }.toSet() != current.map { it.id }.toSet()) return
        updateEditor(
            local = { it.copy(payloads = payloads) },
            persisted = { definition ->
                if (payloads.map(PayloadSlot::id).toSet() == definition.items.map(PayloadSlot::id).toSet()) {
                    definition.copy(items = payloads)
                } else {
                    definition
                }
            },
        )
    }

    /** Appends a session-unique Payload item. */
    fun addPayload(kind: PayloadKind) {
        val slot = newPayloadSlot(kind)
        updateEditor(
            local = { current -> current.copy(payloads = current.payloads + slot) },
            persisted = { current -> current.copy(items = current.items + slot) },
        )
    }

    /** Selects an installed manager option by its stable name. */
    fun setRootManager(name: String) {
        if (mutableUiState.value.editor.rootManagers.none { it.name == name && it.installed }) return
        updateEditor(
            local = { it.copy(rootManager = name) },
            persisted = { it.copy(rootManager = name) },
        )
    }

    /** Persists manager safe mode without altering ordered items. */
    fun setSafeMode(enabled: Boolean) = updateEditor(
        local = { it.copy(safeMode = enabled) },
        persisted = { it.copy(safeMode = enabled) },
    )

    /**
     * Publishes a non-executable preview through the same renderer used by production staging.
     *
     * @return Success with the absolute isolated preview path, or a captured validation/publication failure.
     * @note Preview failure is presentation data and must never escape into the Android main loop.
     */
    fun savePreview(onResult: (Result<String>) -> Unit) {
        val editor = mutableUiState.value.editor
        if (editor.hasIncompletePayload) {
            onResult(Result.failure(IllegalStateException("enabled Payload item is incomplete")))
            return
        }
        val exactSave = savePayloadEditor(editor)
        viewModelScope.launch {
            onResult(runCatching { previewRootScript(exactSave.await()) })
        }
    }

    /** Submits every actor-current edit while publishing an immediate page-local draft. */
    private fun updateEditor(
        local: (PayloadEditorState) -> PayloadEditorState,
        persisted: (PayloadDefinition) -> PayloadDefinition,
    ) {
        val next = local(mutableUiState.value.editor)
        mutableUiState.value = mutableUiState.value.copy(editor = next)
        observeCompletion(savePayloadEditor.transform(persisted))
    }

    /** Reconciles a failed accepted edit from the durable authority without publishing false success. */
    private fun observeCompletion(completion: Deferred<PayloadDefinition>) {
        viewModelScope.launch {
            if (runCatching { completion.await() }.isFailure) refresh()
        }
    }
}
