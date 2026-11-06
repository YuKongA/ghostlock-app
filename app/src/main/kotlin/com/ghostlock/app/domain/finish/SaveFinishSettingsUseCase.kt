package com.ghostlock.app.domain.finish

/** Persists Finish settings through the sole typed repository authority. */
class SaveFinishSettingsUseCase(
    private val port: FinishSettingsPort,
) {
    /** @param settings Complete built-in Finish option projection. */
    operator fun invoke(settings: FinishSettings) {
        port.save(settings.applyTo(port.read()))
    }
}
