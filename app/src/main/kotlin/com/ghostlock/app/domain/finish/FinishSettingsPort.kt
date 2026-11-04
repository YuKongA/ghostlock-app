package com.ghostlock.app.domain.finish

/** Domain boundary for reading and saving non-payload finish options. */
interface FinishSettingsPort {
    /** @return Current persisted finish options. */
    fun read(): FinishSettings

    /**
     * Persists finish options while preserving all payload and handoff fields.
     *
     * @param settings New non-payload option snapshot.
     */
    fun save(settings: FinishSettings)
}
