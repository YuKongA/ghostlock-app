package com.ghostlock.app.data

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.util.Log
import com.ghostlock.app.GhostlockApplication

class BootRootReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        if (intent.action != Intent.ACTION_BOOT_COMPLETED) return
        val settings = (context.applicationContext as GhostlockApplication).bootRootSettings
        val bootCount = settings.currentBootCount() ?: return
        if (!settings.shouldRun(bootCount)) return

        runCatching { context.startForegroundService(Intent(context, BootRootService::class.java)) }
            .onFailure { Log.w("GhostLockBoot", "Could not start boot root service", it) }
    }
}
