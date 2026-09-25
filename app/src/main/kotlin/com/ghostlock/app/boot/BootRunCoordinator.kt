package com.ghostlock.app.boot

import android.content.Context
import com.ghostlock.app.data.BootAutoRootPreferences

object BootRunCoordinator {
    private var scheduledThisProcess = false

    fun scheduleAfterUnlock(context: Context, unusedSource: String = "") {
        val app = context.applicationContext
        val prefs = BootAutoRootPreferences(app)
        if (prefs.disableAutoRunIfRunInterrupted()) return
        if (!prefs.autoRunAtBoot) return
        if (ExploitRunLock.isHeld()) {
            prefs.recordBootSkipRunInProgress()
            return
        }
        synchronized(this) {
            if (scheduledThisProcess) {
                prefs.recordBootSkipAlreadyScheduled()
                return
            }
            scheduledThisProcess = true
        }
        prefs.recordBootScheduled()
        try {
            BootAutoRootService.start(app)
        } catch (error: Throwable) {
            onServiceStartFailed()
            prefs.recordBootFailure(error.message?.take(120) ?: error.javaClass.simpleName)
        }
    }

    fun onServiceStartFailed() {
        synchronized(this) { scheduledThisProcess = false }
    }
}
