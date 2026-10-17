package com.ghostlock.app

import android.app.Application
import com.ghostlock.app.data.AndroidGhostlockRepository
import com.ghostlock.app.domain.repository.GhostlockRepository

/** Application composition root. It is the only place that binds data implementations to domain ports. */
class GhostlockApplication : Application() {
    override fun onCreate() {
        super.onCreate()
        /* v12.5: every launch rescues the native tee — the file survives
         * panics in filesDir but only this app can move it somewhere adb
         * can read. */
        Thread({ OsyncRescue.run() }, "osync-rescue").start()
    }

    fun createRepository(): GhostlockRepository = AndroidGhostlockRepository(this)
}
