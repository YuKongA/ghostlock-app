package com.ghostlock.app

import android.app.Application

/** Android process entry that creates the sole application composition root. */
class GhostLockApp : Application() {
    /** Application-scoped dependency composition root initialized before UI construction. */
    lateinit var container: AppContainer
        private set

    override fun onCreate() {
        super.onCreate()
        container = AppContainer(this)
    }
}
