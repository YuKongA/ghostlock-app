package com.ghostlock.app

import android.app.Application
import android.widget.Toast
import com.ghostlock.app.data.AndroidGhostlockRepository
import com.ghostlock.app.data.BootAutoRootPreferences
import com.ghostlock.app.domain.repository.GhostlockRepository
import com.ghostlock.app.R

/** Application composition root. It is the only place that binds data implementations to domain ports. */
class GhostlockApplication : Application() {
    override fun onCreate() {
        super.onCreate()
        val prefs = BootAutoRootPreferences(this)
        when {
            prefs.disableAutoRunIfRunInterrupted() -> {
                prefs.consumePanicToastPending()
                Toast.makeText(
                    applicationContext,
                    getString(R.string.auto_run_disabled_after_reboot),
                    Toast.LENGTH_LONG,
                ).show()
            }
            prefs.consumePanicToastPending() -> {
                Toast.makeText(
                    applicationContext,
                    getString(R.string.auto_run_disabled_after_reboot),
                    Toast.LENGTH_LONG,
                ).show()
            }
        }
    }

    fun createRepository(): GhostlockRepository = AndroidGhostlockRepository(this)
}
