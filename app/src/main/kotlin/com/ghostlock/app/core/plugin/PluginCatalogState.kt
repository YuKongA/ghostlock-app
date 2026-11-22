package com.ghostlock.app.core.plugin

import com.ghostlock.plugin.core.PluginCatalogSnapshot

/** Application-scoped bootstrap state for the single installed plugin catalog. */
sealed interface PluginCatalogState {
    /** Builtin reconciliation and installed-object validation have not reached a terminal state. */
    data object Initializing : PluginCatalogState

    /** One immutable, fully validated catalog generation. */
    data class Ready(val snapshot: PluginCatalogSnapshot) : PluginCatalogState

    /** Bounded bootstrap failure; membership is empty and run admission remains closed. */
    data class Failed(val reason: String) : PluginCatalogState {
        init {
            require(reason.isNotBlank()) { "catalog failure reason must not be blank" }
            require(reason.encodeToByteArray().size <= MAX_REASON_BYTES) {
                "catalog failure reason exceeds $MAX_REASON_BYTES UTF-8 bytes"
            }
            require(reason.none { it == '\n' || it == '\r' || it.isISOControl() }) {
                "catalog failure reason must be single-line printable text"
            }
        }
    }

    private companion object {
        const val MAX_REASON_BYTES = 4096
    }
}
