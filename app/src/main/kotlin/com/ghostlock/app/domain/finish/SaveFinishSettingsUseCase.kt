package com.ghostlock.app.domain.finish

import kotlinx.coroutines.Deferred

/** Persists Finish settings through the sole typed repository authority. */
class SaveFinishSettingsUseCase(
    private val port: FinishSettingsPort,
) {
    /** @param settings Complete built-in Finish option projection. */
    operator fun invoke(settings: FinishSettings): Deferred<FinishDefinition> =
        port.submit(settings::applyTo)
}
