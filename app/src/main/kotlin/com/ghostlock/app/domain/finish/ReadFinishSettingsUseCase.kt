package com.ghostlock.app.domain.finish

/** Reads current Finish settings from the canonical typed definition. */
class ReadFinishSettingsUseCase(
    private val port: FinishSettingsPort,
) {
    /** @return Current built-in Finish option projection. */
    suspend operator fun invoke(): FinishSettings = FinishSettings.from(port.snapshot().await())
}
