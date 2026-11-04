package com.ghostlock.app.domain.update

/** Checks the configured application update source through a transport-neutral port. */
class CheckAppUpdateUseCase(
    private val port: AppUpdatePort,
) {
    /** @return Latest known release, or an empty result when no update source is wired. */
    suspend operator fun invoke(): AppUpdateInfo = port.check()
}
