package com.ghostlock.app.composition.backend

import com.ghostlock.app.backend.declarations.BackendRuntime
import com.ghostlock.app.backend.declarations.BackendRuntimeEntry
import com.ghostlock.app.domain.backend.BackendDescriptor
import com.ghostlock.app.ui.backend.BackendUi
import com.ghostlock.app.ui.backend.BackendUiEntry

/**
 * Sole outer composition authority for one built-in backend.
 *
 * @property descriptor Stable domain facts.
 * @property runtime Availability and fresh Flow creation owned by the backend layer.
 * @property ui Android presentation owned by the UI layer.
 * @note Inner callers receive only [BackendRuntimeEntry] or [BackendUiEntry], never this outer object.
 */
internal data class BackendDefinition(
    val descriptor: BackendDescriptor,
    val runtime: BackendRuntime,
    val ui: BackendUi,
) {
    /** Returns a backend-layer-only projection for execution callers. */
    fun runtimeEntry(): BackendRuntimeEntry = BackendRuntimeEntry(descriptor, runtime)

    /** Returns a UI-layer-only projection for application presentation callers. */
    fun uiEntry(): BackendUiEntry = BackendUiEntry(descriptor, ui)
}
