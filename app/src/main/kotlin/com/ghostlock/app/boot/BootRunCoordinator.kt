package com.ghostlock.app.boot

import android.content.Context
import android.os.SystemClock
import com.ghostlock.app.data.BootAutoRootPreferences

object BootRunCoordinator {
    private var scheduledThisProcess = false

    fun scheduleAfterUnlock(context: Context, source: String) {
        val app = context.applicationContext
        val prefs = BootAutoRootPreferences(app)
        if (!prefs.autoRunAtBoot) return
        if (ExploitRunLock.isHeld()) {
            prefs.recordBootSkip("run already in progress ($source)")
            return
        }
        synchronized(this) {
            if (scheduledThisProcess) {
                prefs.recordBootSkip("already scheduled ($source)")
                return
            }
            scheduledThisProcess = true
        }
        prefs.recordBootTrigger(source, SystemClock.elapsedRealtime())
        try {
            BootAutoRootService.start(app)
        } catch (error: Throwable) {
            synchronized(this) { scheduledThisProcess = false }
            prefs.recordBootFailure(error.message?.take(120) ?: error.javaClass.simpleName)
    fun onServiceStartFailed() {
        synchronized(this) { scheduledThisProcess = false }
    }
}
