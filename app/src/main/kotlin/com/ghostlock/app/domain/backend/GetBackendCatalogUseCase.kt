package com.ghostlock.app.domain.backend

/** Reads backend runtime facts through the domain catalog boundary. */
class GetBackendCatalogUseCase(
    private val port: BackendCatalogPort,
) {
    /** @return One immutable current backend-catalog snapshot. */
    operator fun invoke(): BackendRegistrySnapshot = port.read()
}
