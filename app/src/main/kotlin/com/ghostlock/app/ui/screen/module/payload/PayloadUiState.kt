package com.ghostlock.app.ui.screen.module.payload

import androidx.compose.runtime.Immutable
import com.ghostlock.app.domain.payload.PayloadEditorState
import com.ghostlock.app.domain.payload.PayloadKind
import com.ghostlock.app.domain.payload.PayloadSlot

/** Shared semantic state rendered by both finish-editor skins and the path-page summary card. */
@Immutable
data class PayloadUiState(
    val editor: PayloadEditorState,
    val editMode: Boolean = false,
    val showAddDialog: Boolean = false,
)

/**
 * Shared Payload-editor actions; renderers own only platform presentation such as pickers and Toasts.
 *
 * @property onSavePreview Returns the published path or a captured publication failure without throwing into UI.
 */
@Immutable
data class PayloadActions(
    val onBack: () -> Unit,
    val onToggleEditMode: () -> Unit,
    val onSetAddDialogVisible: (Boolean) -> Unit,
    val onUpdatePayload: (PayloadSlot) -> Unit,
    val onDeletePayload: (String) -> Unit,
    val onMovePayloads: (List<PayloadSlot>) -> Unit,
    val onAddPayload: (PayloadKind) -> Unit,
    val onSetRootManager: (String) -> Unit,
    val onSetSafeMode: (Boolean) -> Unit,
    val onSavePreview: () -> Result<String>,
)
