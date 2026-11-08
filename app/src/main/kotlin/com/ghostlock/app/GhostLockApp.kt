package com.ghostlock.app

import android.app.Application

/** Android process entry that creates the sole UI composition root only in the package main process. */
class GhostLockApp : Application() {
    private var mainContainer: AppContainer? = null

    /** Main-process application composition root; secondary-process access fails closed. */
    val container: AppContainer
        get() = checkNotNull(mainContainer) { "UI container is unavailable in this process" }

    /** True only when this process owns the UI DataStores and composition root. */
    val hasUiContainer: Boolean
        get() = mainContainer != null

    override fun onCreate() {
        super.onCreate()
        if (isMainApplicationProcess(Application.getProcessName(), packageName)) {
            mainContainer = AppContainer(this)
        }
    }

    companion object {
        /** @return True only for the exact package process, never `:flowhost` or another suffix. */
        fun isMainApplicationProcess(processName: String?, packageName: String): Boolean =
            processName == packageName
    }
}
