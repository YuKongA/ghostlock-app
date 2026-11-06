package com.ghostlock.app.data.backend

import com.ghostlock.app.domain.backend.BackendCatalogPort
import com.ghostlock.app.domain.backend.BackendRegistrySnapshot
import com.ghostlock.app.domain.backend.BackendAvailability
import com.ghostlock.app.domain.backend.BackendSpec

/**
 * Process-local adapter over the single registry copy captured by AppContainer.
 *
 * @param registryEntries Immutable registry-order copy captured at application composition time.
 * @param selectedKey Reads the current stable selection without leaking its global owner inward.
 * @param availability Reads resource-free current readiness by stable key.
 */
class BackendCatalogAdapter(
    registryEntries: List<BackendSpec>,
    private val selectedKey: () -> String?,
    private val availability: (String) -> BackendAvailability?,
) : BackendCatalogPort {
    private val entries = registryEntries.toList()
    /** @return Fresh readiness facts over immutable registry membership. */
    override fun read(): BackendRegistrySnapshot = BackendRegistrySnapshot(
        entries = entries,
        availability = entries.mapNotNull { spec ->
            availability(spec.key)?.let { current -> spec.key to current }
        }.toMap(),
        selectedKey = selectedKey(),
    )
}
