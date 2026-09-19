package com.ghostlock.app.data

import android.content.Context
import android.content.Intent
import androidx.core.content.edit

class BootAutoRootPreferences(context: Context) {
    private val appContext = context.applicationContext
    private val deviceContext = appContext.createDeviceProtectedStorageContext()
    private val prefs = deviceContext.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)

    init {
        migrateFromCredentialStorageIfNeeded()
    }

    var autoRunAtBoot: Boolean
        get() = prefs.getBoolean(KEY_AUTO_RUN_AT_BOOT, false)
        set(value) = prefs.edit { putBoolean(KEY_AUTO_RUN_AT_BOOT, value) }

    var runBeforeUnlock: Boolean
        get() = prefs.getBoolean(KEY_RUN_BEFORE_UNLOCK, false)
        set(value) = prefs.edit { putBoolean(KEY_RUN_BEFORE_UNLOCK, value) }

    var maxAttempts: Int
        get() = prefs.getInt(KEY_MAX_ATTEMPTS, DEFAULT_MAX_ATTEMPTS).coerceIn(MIN_ATTEMPTS, MAX_ATTEMPTS)
        set(value) = prefs.edit {
            putInt(KEY_MAX_ATTEMPTS, value.coerceIn(MIN_ATTEMPTS, MAX_ATTEMPTS))
        }

    fun shouldHandleBootAction(action: String?): Boolean {
        if (!autoRunAtBoot) return false
        return when (action) {
            Intent.ACTION_LOCKED_BOOT_COMPLETED -> runBeforeUnlock
            Intent.ACTION_BOOT_COMPLETED -> !runBeforeUnlock
            else -> false
        }
    }

    /** Copies CPU pair (and other CE-only runtime prefs) into device-protected storage for direct boot. */
    fun syncRuntimeFromApp() {
        val credential = appContext.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        val cpuPair = credential.getString(KEY_CPU_PAIR, null) ?: return
        prefs.edit { putString(KEY_CPU_PAIR, cpuPair) }
    }

    private fun migrateFromCredentialStorageIfNeeded() {
        if (prefs.contains(KEY_AUTO_RUN_AT_BOOT)) return
        val credential = appContext.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        if (!credential.contains(KEY_AUTO_RUN_AT_BOOT) &&
            !credential.contains(KEY_MAX_ATTEMPTS) &&
            !credential.contains(KEY_RUN_BEFORE_UNLOCK)
        ) {
            return
        }
        prefs.edit {
            putBoolean(KEY_AUTO_RUN_AT_BOOT, credential.getBoolean(KEY_AUTO_RUN_AT_BOOT, false))
            putBoolean(KEY_RUN_BEFORE_UNLOCK, credential.getBoolean(KEY_RUN_BEFORE_UNLOCK, false))
            putInt(
                KEY_MAX_ATTEMPTS,
                credential.getInt(KEY_MAX_ATTEMPTS, DEFAULT_MAX_ATTEMPTS)
                    .coerceIn(MIN_ATTEMPTS, MAX_ATTEMPTS),
            )
            credential.getString(KEY_CPU_PAIR, null)?.let { putString(KEY_CPU_PAIR, it) }
        }
    }

    companion object {
        const val PREFS_NAME = "ghostlock_prefs"
        const val KEY_AUTO_RUN_AT_BOOT = "auto_run_at_boot"
        const val KEY_RUN_BEFORE_UNLOCK = "run_before_unlock"
        const val KEY_MAX_ATTEMPTS = "max_boot_attempts"
        const val KEY_CPU_PAIR = "cpu_pair"
        const val DEFAULT_MAX_ATTEMPTS = 3
        const val MIN_ATTEMPTS = 1
        const val MAX_ATTEMPTS = 10
    }
}
