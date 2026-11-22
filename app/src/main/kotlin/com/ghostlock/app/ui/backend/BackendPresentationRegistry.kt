package com.ghostlock.app.ui.backend

import com.ghostlock.app.composition.backend.BackendCatalogView

/** Host-owned Android presentation adapters keyed only by existing catalog membership. */
internal class BackendPresentationRegistry private constructor(
    presentations: Map<String, BackendUi>,
) {
    private val presentations = presentations.toMap().also { entries ->
        require(entries.keys.none(String::isBlank)) { "blank backend presentation key" }
    }

    fun entries(view: BackendCatalogView): List<BackendUiEntry> = view.projections.map { projection ->
        val key = projection.descriptor.key
        BackendUiEntry(
            descriptor = projection.descriptor,
            ui = requireNotNull(presentations[key]) { "missing Host presentation for $key" },
        )
    }

    companion object {
        fun builtin(): BackendPresentationRegistry = BackendPresentationRegistry(
            mapOf("DIAGNOSE" to DiagnoseBackendUi),
        )
    }
}
