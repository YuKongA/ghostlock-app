package com.ghostlock.app.boot

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import com.ghostlock.app.data.BootAutoRootPreferences

class BootCompletedReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent?) {
        try {
            val action = intent?.action ?: return
            val prefs = BootAutoRootPreferences(context)
            if (!prefs.shouldHandleBootAction(action)) return
            val beforeUnlock = action == Intent.ACTION_LOCKED_BOOT_COMPLETED
            BootAutoRootService.start(context.applicationContext, beforeUnlock)
        } catch (_: Throwable) {
            // Never crash the system broadcast path.
        }
    }
}
