package com.ghostlock.app.data

import android.content.Context
import android.content.Intent
import androidx.core.content.edit
import com.ghostlock.app.boot.BootRunResult
import com.ghostlock.app.boot.SkipReason

class BootAutoRootPreferences(context: Context) {
    private val prefs = context.applicationContext.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)

    var autoRunAtBoot: Boolean
        get() = prefs.getBoolean(KEY_AUTO_RUN_AT_BOOT, false)
        set(value) = prefs.edit { putBoolean(KEY_AUTO_RUN_AT_BOOT, value) }

    var maxAttempts: Int
        get() = prefs.getInt(KEY_MAX_ATTEMPTS, DEFAULT_MAX_ATTEMPTS).coerceIn(MIN_ATTEMPTS, MAX_ATTEMPTS)
        set(value) = prefs.edit {
            putInt(KEY_MAX_ATTEMPTS, value.coerceIn(MIN_ATTEMPTS, MAX_ATTEMPTS))
        }

    var notifyProgressEnabled: Boolean
        get() = prefs.getBoolean(KEY_NOTIFY_PROGRESS, true)
        set(value) = prefs.edit { putBoolean(KEY_NOTIFY_PROGRESS, value) }

    var notifyProgressDetailed: Boolean
        get() = prefs.getBoolean(KEY_NOTIFY_PROGRESS_DETAILED, true)
        set(value) = prefs.edit { putBoolean(KEY_NOTIFY_PROGRESS_DETAILED, value) }

    var notifyResultSuccess: Boolean
        get() = prefs.getBoolean(KEY_NOTIFY_RESULT_SUCCESS, true)
        set(value) = prefs.edit { putBoolean(KEY_NOTIFY_RESULT_SUCCESS, value) }

    var notifyResultFailure: Boolean
        get() = prefs.getBoolean(KEY_NOTIFY_RESULT_FAILURE, true)
        set(value) = prefs.edit { putBoolean(KEY_NOTIFY_RESULT_FAILURE, value) }

    var notifyResultSkipped: Boolean
        get() = prefs.getBoolean(KEY_NOTIFY_RESULT_SKIPPED, true)
        set(value) = prefs.edit { putBoolean(KEY_NOTIFY_RESULT_SKIPPED, value) }

    val lastBootStatus: String
        get() = prefs.getString(KEY_LAST_BOOT_STATUS, "").orEmpty()

    fun shouldShowResult(result: BootRunResult): Boolean = when (result) {
        BootRunResult.Success -> notifyResultSuccess
        BootRunResult.FailedNotRoot -> notifyResultFailure
        is BootRunResult.StoppedSafely -> notifyResultFailure
        is BootRunResult.Skipped -> when (result.reason) {
            SkipReason.Disabled -> false
            SkipReason.UnsupportedKernel, SkipReason.NoCpuPair, SkipReason.RunInProgress -> notifyResultSkipped
        }
    }

    fun shouldScheduleBootRun(action: String?): Boolean {
        if (!autoRunAtBoot) return false
        return action == Intent.ACTION_BOOT_COMPLETED || action == Intent.ACTION_USER_UNLOCKED
    }

    fun recordBootTrigger(source: String, elapsedRealtime: Long) {
        prefs.edit {
            putString(KEY_LAST_BOOT_STATUS, "Scheduled ($source)")
            putLong(KEY_LAST_BOOT_AT, System.currentTimeMillis())
            putLong(KEY_LAST_BOOT_ELAPSED, elapsedRealtime)
        }
    }

    fun recordBootFailure(message: String) {
        prefs.edit { putString(KEY_LAST_BOOT_STATUS, "Failed: $message") }
    }

    fun recordBootSkip(message: String) {
        prefs.edit { putString(KEY_LAST_BOOT_STATUS, "Skipped: $message") }
    }

    fun recordBootFinished(summary: String) {
        prefs.edit { putString(KEY_LAST_BOOT_STATUS, summary) }
    }

    companion object {
        const val PREFS_NAME = "ghostlock_prefs"
        const val KEY_AUTO_RUN_AT_BOOT = "auto_run_at_boot"
        const val KEY_MAX_ATTEMPTS = "max_boot_attempts"
        const val KEY_CPU_PAIR = "cpu_pair"
        const val KEY_NOTIFY_PROGRESS = "notify_progress"
        const val KEY_NOTIFY_PROGRESS_DETAILED = "notify_progress_detailed"
        const val KEY_NOTIFY_RESULT_SUCCESS = "notify_result_success"
        const val KEY_NOTIFY_RESULT_FAILURE = "notify_result_failure"
        const val KEY_NOTIFY_RESULT_SKIPPED = "notify_result_skipped"
        const val KEY_LAST_BOOT_STATUS = "last_boot_status"
        const val KEY_LAST_BOOT_AT = "last_boot_at"
        const val KEY_LAST_BOOT_ELAPSED = "last_boot_elapsed"
        const val DEFAULT_MAX_ATTEMPTS = 3
        const val MIN_ATTEMPTS = 1
        const val MAX_ATTEMPTS = 10
    }
}
