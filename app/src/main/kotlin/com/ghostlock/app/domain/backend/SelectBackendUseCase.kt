package com.ghostlock.app.domain.backend

/** Selects a backend through the persistence-neutral domain command boundary. */
class SelectBackendUseCase(
    private val port: BackendSelectionPort,
) {
    /**
     * @param key Stable backend identity from the catalog.
     * @return True when selection and persistence succeeded.
     */
    operator fun invoke(key: String): Boolean = port.select(key)
}
