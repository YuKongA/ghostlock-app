package com.ghostlock.app.domain.finish

/** Persists non-payload finish options through the domain boundary. */
class SaveFinishSettingsUseCase(
    private val port: FinishSettingsPort,
) {
    /** @param settings Complete non-payload finish option snapshot. */
    operator fun invoke(settings: FinishSettings) {
        port.save(settings)
    }
}
