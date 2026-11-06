package com.ghostlock.app.domain.backend

/**
 * Immutable application-process view of the backend registry.
 *
 * @property entries Neutral immutable specs in registry order.
 * @property availability Dynamic readiness facts keyed by stable backend key.
 * @property selectedKey Current selection key observed when this snapshot was read.
 */
data class BackendRegistrySnapshot(
    val entries: List<BackendSpec>,
    val availability: Map<String, BackendAvailability>,
    val selectedKey: String?,
) {
    /**
     * Resolves the active backend without exposing registry globals to presentation.
     *
     * @return Selected selectable entry, the first selectable fallback, or null for an empty registry.
     */
    fun selectedBackend(): BackendSpec? =
        entries.firstOrNull { it.selectable && it.key == selectedKey }
            ?: entries.firstOrNull { it.selectable }
            ?: entries.firstOrNull()

    /** Returns current readiness for one captured key, or a fail-closed unavailable fact. */
    fun availabilityFor(key: String): BackendAvailability = availability[key]
        ?: BackendAvailability.NotReady(
            listOf(
                BackendAvailabilityReason(
                    code = "availability-missing",
                    message = "Backend readiness is unavailable.",
                ),
            ),
        )
}
