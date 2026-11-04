package com.ghostlock.app.domain.backend

/** Domain boundary for reading the process-local backend catalog. */
interface BackendCatalogPort {
    /**
     * Projects one current catalog snapshot.
     *
     * @return Immutable registry entries plus the current stable selection key.
     */
    fun read(): BackendRegistrySnapshot
}
