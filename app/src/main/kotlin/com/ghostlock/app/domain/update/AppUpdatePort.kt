package com.ghostlock.app.domain.update

/** Domain boundary for obtaining the latest published GhostLock application version. */
interface AppUpdatePort {
    /**
     * Reads the latest available application release.
     *
     * @return Published release data, or an empty result when the source is unavailable or disabled.
     */
    suspend fun check(): AppUpdateInfo
}
