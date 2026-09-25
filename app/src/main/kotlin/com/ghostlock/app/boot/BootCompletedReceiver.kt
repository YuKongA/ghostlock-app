package com.ghostlock.app.boot

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import com.ghostlock.app.data.BootAutoRootPreferences

class BootCompletedReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent?) {
        try {
            val action = intent?.action ?: return
            if (!BootAutoRootPreferences(context).shouldScheduleBootRun(action)) return
            BootRunCoordinator.scheduleAfterUnlock(context, action)
        } catch (error: Throwable) {
            BootAutoRootPreferences(context).recordBootFailure(
                error.message?.take(120) ?: error.javaClass.simpleName,
            )
        }
    }
}
