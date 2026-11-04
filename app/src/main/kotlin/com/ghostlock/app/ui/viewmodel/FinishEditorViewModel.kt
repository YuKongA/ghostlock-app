package com.ghostlock.app.ui.viewmodel

import androidx.lifecycle.ViewModel
import com.ghostlock.app.domain.finish.FinishEditorState
import com.ghostlock.app.domain.finish.FinishPayloadKind
import com.ghostlock.app.domain.finish.FinishPayloadSlot
import com.ghostlock.app.domain.finish.PreviewFinishScriptUseCase
import com.ghostlock.app.domain.finish.ReadFinishEditorUseCase
import com.ghostlock.app.domain.finish.SaveFinishEditorUseCase
import com.ghostlock.app.domain.finish.newFinishPayloadSlot
import com.ghostlock.app.ui.screen.module.backends.FinishEditorUiState
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/**
 * Owns finish-editor state and persistence without exposing authoritative config objects to Compose.
 *
 * @param readFinishEditor Reads persisted payload and manager environment state.
 * @param saveFinishEditor Persists editor-owned fields while preserving handoff-only fields.
 * @param previewFinishScript Generates the unchanged existing script preview.
 */
class FinishEditorViewModel(
    private val readFinishEditor: ReadFinishEditorUseCase,
    private val saveFinishEditor: SaveFinishEditorUseCase,
    private val previewFinishScript: PreviewFinishScriptUseCase,
) : ViewModel() {
    private val mutableUiState = MutableStateFlow(FinishEditorUiState(readFinishEditor()))
    val uiState: StateFlow<FinishEditorUiState> = mutableUiState.asStateFlow()

    /** Reloads persisted editor values without changing transient UI mode. */
    fun refresh() {
        mutableUiState.value = mutableUiState.value.copy(editor = readFinishEditor())
    }

    /** Reloads editor values and resets page-local edit/dialog state for a fresh navigation entry. */
    fun openEditor() {
        mutableUiState.value = FinishEditorUiState(editor = readFinishEditor())
    }

    /** Toggles list editing without changing persisted payload data. */
    fun toggleEditMode() {
        mutableUiState.value = mutableUiState.value.copy(
            editMode = !mutableUiState.value.editMode,
            showAddDialog = false,
        )
    }

    /** Controls the add-payload dialog as transient presentation state. */
    fun setAddDialogVisible(visible: Boolean) {
        mutableUiState.value = mutableUiState.value.copy(showAddDialog = visible)
    }

    /** Replaces one payload by stable id and persists the resulting ordered list. */
    fun updatePayload(slot: FinishPayloadSlot) = updateEditor { current ->
        current.copy(payloads = current.payloads.map { if (it.id == slot.id) slot else it })
    }

    /** Removes one payload by stable id and persists the resulting ordered list. */
    fun deletePayload(id: String) = updateEditor { current ->
        current.copy(payloads = current.payloads.filterNot { it.id == id })
    }

    /** Persists the caller-provided ordering only when it contains the same stable slot identities. */
    fun movePayloads(payloads: List<FinishPayloadSlot>) {
        val current = mutableUiState.value.editor.payloads
        if (payloads.map { it.id }.toSet() != current.map { it.id }.toSet()) return
        updateEditor { it.copy(payloads = payloads) }
    }

    /** Appends a session-unique payload slot of the requested kind. */
    fun addPayload(kind: FinishPayloadKind) = updateEditor { current ->
        current.copy(payloads = current.payloads + newFinishPayloadSlot(kind))
    }

    /** Selects an installed manager option by stable name. */
    fun setRootManager(name: String) {
        if (mutableUiState.value.editor.rootManagers.none { it.name == name && it.installed }) return
        updateEditor { it.copy(rootManager = name) }
    }

    /** Persists safe mode without altering payload or manager selection. */
    fun setSafeMode(enabled: Boolean) = updateEditor { it.copy(safeMode = enabled) }

    /**
     * Generates the preview for the current complete editor state.
     *
     * @return Absolute preview path.
     * @throws IllegalStateException When an enabled payload is incomplete.
     */
    fun savePreview(): String {
        val editor = mutableUiState.value.editor
        check(!editor.hasIncompletePayload) { "enabled finish payload is incomplete" }
        return previewFinishScript(editor)
    }

    /** Persists one atomic editor transform and publishes the same immutable projection. */
    private fun updateEditor(transform: (FinishEditorState) -> FinishEditorState) {
        val next = transform(mutableUiState.value.editor)
        saveFinishEditor(next)
        mutableUiState.value = mutableUiState.value.copy(editor = next)
    }
}
