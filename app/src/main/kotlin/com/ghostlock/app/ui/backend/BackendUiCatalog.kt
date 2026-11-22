package com.ghostlock.app.ui.backend

import com.ghostlock.app.domain.backend.BackendAvailability
import com.ghostlock.app.domain.backend.BackendAvailabilityReason

/**
 * Process-local UI projection of the one terminal plugin catalog generation.
 *
 * @param entries Reads Host presentation adapters for current READY catalog membership.
 * @param selectedKey Reads the current stable selection without owning persistence.
 * @param catalogFailureReason Reads the bounded FAILED reason without inventing a backend row.
 * @param availability Reads resource-free catalog compatibility by stable key.
 * @param text Resolves ordinary Android string resources at the composition boundary.
 */
class BackendUiCatalog(
    private val entries: () -> List<BackendUiEntry>,
    private val selectedKey: () -> String?,
    private val catalogFailureReason: () -> String?,
    private val availability: (String) -> BackendAvailability?,
    private val text: (Int) -> String,
    private val catalogFailureText: (String) -> String,
) {
    /** Immutable READY-generation entries; an unready/failed catalog projects an empty list. */
    val entriesSnapshot: List<BackendUiEntry>
        get() = entries().toList().also { snapshot ->
            require(snapshot.map { entry -> entry.descriptor.key }.toSet().size == snapshot.size) {
                "duplicate backend UI key"
            }
        }

    /** Resolves one entry by exact stable key. */
    fun entry(key: String?): BackendUiEntry? = entriesSnapshot.firstOrNull { candidate ->
        candidate.descriptor.key == key
    }

    /** Resolves only an exact persisted selectable entry; list order never manufactures selection. */
    fun selectedEntry(): BackendUiEntry? {
        val key = selectedKey()
        return entriesSnapshot.firstOrNull { entry ->
            entry.descriptor.selectable && entry.descriptor.key == key
        }
    }

    /** Bounded bootstrap failure shown outside the membership list. */
    fun failureReason(): String? = catalogFailureReason()

    /** Localizes the terminal catalog failure without projecting it as a backend entry. */
    fun failureStatus(): String? = failureReason()?.let(catalogFailureText)

    /** Localizes the explicit absence of a backend choice. */
    fun noBackendStatus(): String = text(com.ghostlock.app.R.string.backend_run_no_backend)

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
