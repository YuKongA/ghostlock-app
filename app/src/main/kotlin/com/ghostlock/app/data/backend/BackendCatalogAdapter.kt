package com.ghostlock.app.data.backend

import com.ghostlock.app.backend.declarations.BackendSpec
import com.ghostlock.app.domain.backend.BackendCatalogPort
import com.ghostlock.app.domain.backend.BackendRegistryIdentity
import com.ghostlock.app.domain.backend.BackendRegistrySnapshot

/**
 * Process-local adapter over the single registry copy captured by AppContainer.
 *
 * @param registryEntries Immutable registry-order copy captured at application composition time.
 * @param selectedKey Reads the current stable selection without leaking its global owner inward.
 * @param presentation Maps outer plugin declarations into pure domain values.
 */
class BackendCatalogAdapter(
    registryEntries: List<BackendSpec>,
    private val identity: BackendRegistryIdentity,
    private val selectedKey: () -> String?,
    private val presentation: BackendPresentationAdapter,
) : BackendCatalogPort {
    private val entries = registryEntries.toList()
    /** @return Fresh runtime projections over the immutable registry membership snapshot. */
    override fun read(): BackendRegistrySnapshot = BackendRegistrySnapshot(
        version = identity.version,
        contentHash = identity.contentHash,
        entries = entries.map(presentation::project),
        selectedKey = selectedKey(),
    )
}
