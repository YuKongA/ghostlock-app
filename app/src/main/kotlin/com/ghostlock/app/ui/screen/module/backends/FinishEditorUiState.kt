package com.ghostlock.app.ui.screen.module.backends

import androidx.compose.runtime.Immutable
import com.ghostlock.app.domain.finish.FinishEditorState
import com.ghostlock.app.domain.finish.FinishPayloadKind
import com.ghostlock.app.domain.finish.FinishPayloadSlot

/** Shared semantic state rendered by both finish-editor skins and the path-page summary card. */
@Immutable
data class FinishEditorUiState(
    val editor: FinishEditorState,
    val editMode: Boolean = false,
    val showAddDialog: Boolean = false,
)

/** Shared finish-editor actions; renderers own only platform presentation such as pickers and Toasts. */
@Immutable
data class FinishEditorActions(
    val onBack: () -> Unit,
    val onToggleEditMode: () -> Unit,
    val onSetAddDialogVisible: (Boolean) -> Unit,
    val onUpdatePayload: (FinishPayloadSlot) -> Unit,
    val onDeletePayload: (String) -> Unit,
    val onMovePayloads: (List<FinishPayloadSlot>) -> Unit,
    val onAddPayload: (FinishPayloadKind) -> Unit,
    val onSetRootManager: (String) -> Unit,
    val onSetSafeMode: (Boolean) -> Unit,
    val onSavePreview: () -> String,
)
