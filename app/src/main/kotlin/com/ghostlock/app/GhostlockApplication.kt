package com.ghostlock.app

import android.app.Application
import com.ghostlock.app.data.AndroidGhostlockRepository
import com.ghostlock.app.data.BootRootSettings
import com.ghostlock.app.domain.repository.GhostlockRepository
import kotlinx.coroutines.sync.Mutex

/** Application composition root. It is the only place that binds data implementations to domain ports. */
class GhostlockApplication : Application() {
    internal val bootRootSettings by lazy { BootRootSettings(this) }
    private val exploitRunLock = Mutex()

    fun createRepository(silentManagerLaunch: Boolean = false): GhostlockRepository =
        AndroidGhostlockRepository(this, bootRootSettings, exploitRunLock, silentManagerLaunch)
}
