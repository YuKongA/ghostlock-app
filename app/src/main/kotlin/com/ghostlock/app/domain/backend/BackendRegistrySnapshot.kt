package com.ghostlock.app.domain.backend

/**
 * Immutable application-process view of the backend registry.
 *
 * @property version Stable content version of the captured registry entries.
 * @property contentHash Full membership hash used by outer cross-process launch validation.
 * @property entries Runtime projections in registry order.
 * @property selectedKey Current selection key observed when this snapshot was read.
 */
data class BackendRegistrySnapshot(
    val version: Long,
    val contentHash: String,
    val entries: List<BackendRuntimeSpec>,
    val selectedKey: String?,
) {
    /**
     * Resolves the active backend without exposing registry globals to presentation.
     *
     * @return Selected selectable entry, the first selectable fallback, or null for an empty registry.
     */
    fun selectedBackend(): BackendRuntimeSpec? =
        entries.firstOrNull { it.selectable && it.key == selectedKey }
            ?: entries.firstOrNull { it.selectable }
            ?: entries.firstOrNull()
}
