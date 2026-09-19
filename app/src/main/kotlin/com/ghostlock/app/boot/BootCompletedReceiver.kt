package com.ghostlock.app.boot

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import com.ghostlock.app.data.BootAutoRootPreferences

class BootCompletedReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent?) {
        try {
            if (intent?.action != Intent.ACTION_BOOT_COMPLETED) return
            if (!BootAutoRootPreferences(context).shouldHandleBootAction(intent.action)) return
            BootAutoRootService.start(context.applicationContext)
        } catch (_: Throwable) {
        }
    }
}
