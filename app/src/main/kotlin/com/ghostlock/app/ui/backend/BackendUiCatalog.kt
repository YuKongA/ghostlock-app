package com.ghostlock.app.ui.backend

import com.ghostlock.app.domain.backend.BackendAvailability
import com.ghostlock.app.domain.backend.BackendAvailabilityReason

/**
 * Process-local UI projection of the sole immutable backend definition snapshot.
 *
 * @param entries UI entries derived by the outer composition registry.
 * @param selectedKey Reads the current stable selection without owning persistence.
 * @param availability Reads current runtime readiness by stable key.
 * @param text Resolves ordinary Android string resources at the composition boundary.
 */
class BackendUiCatalog(
    entries: List<BackendUiEntry>,
    private val selectedKey: () -> String?,
    private val availability: (String) -> BackendAvailability?,
    private val text: (Int) -> String,
) {
    /** Immutable entries in registry order; this is a projection rather than membership authority. */
    val entries: List<BackendUiEntry> = entries.toList().also { snapshot ->
        require(snapshot.map { entry -> entry.descriptor.key }.toSet().size == snapshot.size) {
            "duplicate backend UI key"
        }
    }

    /** Resolves one entry by exact stable key. */
    fun entry(key: String?): BackendUiEntry? = entries.firstOrNull { candidate ->
        candidate.descriptor.key == key
    }

    /** Resolves the selected selectable entry, then the first selectable or first visible fallback. */
    fun selectedEntry(): BackendUiEntry? {
        val key = selectedKey()
        return entries.firstOrNull { entry -> entry.descriptor.selectable && entry.descriptor.key == key }
            ?: entries.firstOrNull { entry -> entry.descriptor.selectable }
            ?: entries.firstOrNull()
    }

    /** Returns current readiness, failing closed when the runtime projection is absent. */
    fun availabilityFor(key: String): BackendAvailability = availability(key)
        ?: BackendAvailability.NotReady(
            listOf(
                BackendAvailabilityReason(
                    code = "availability-missing",
                    message = "Backend readiness is unavailable.",
                ),
            ),
        )

    /** Resolves one entry's localized selector name. */
    fun displayName(entry: BackendUiEntry): String = text(entry.ui.displayNameRes)

    /** Resolves one entry's localized category label. */
    fun categoryLabel(entry: BackendUiEntry): String = text(entry.ui.categoryLabelRes)
}
