package com.ghostlock.app.boot

import android.content.Context
import com.ghostlock.app.R
import com.ghostlock.app.data.BootAutoRootPreferences
import com.ghostlock.app.ui.RunSessionProgress

object BootRunNotificationProgress {
    fun resetSession() = RunSessionProgress.reset()

    fun progressBody(context: Context, prefs: BootAutoRootPreferences): String {
        val title = context.getString(R.string.session_running)
        if (!prefs.notifyProgressEnabled) return title
        if (!prefs.notifyProgressDetailed) return title
        return RunSessionProgress.notificationBody(context)
    }
}
