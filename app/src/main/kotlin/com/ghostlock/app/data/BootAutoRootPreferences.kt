package com.ghostlock.app.data

import android.content.Context
import android.content.Intent
import androidx.core.content.edit
import com.ghostlock.app.R
import com.ghostlock.app.boot.BootRunResult
import com.ghostlock.app.boot.SkipReason

class BootAutoRootPreferences(context: Context) {
    private val appContext = context.applicationContext
    private val prefs = appContext.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)

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

    fun formatLastBootStatus(context: Context): String {
        val kind = prefs.getString(KEY_LAST_BOOT_KIND, null)
        if (kind.isNullOrBlank()) {
            return context.getString(R.string.boot_last_status_empty)
        }
        val extra = prefs.getString(KEY_LAST_BOOT_EXTRA, "").orEmpty()
        return when (kind) {
            KIND_SCHEDULED -> context.getString(R.string.boot_status_scheduled)
            KIND_FAILED -> context.getString(R.string.boot_status_failed, extra)
            KIND_SKIP_ALREADY -> context.getString(R.string.boot_status_skip_already)
            KIND_SKIP_IN_PROGRESS -> context.getString(R.string.boot_status_skip_in_progress)
            KIND_FINISHED_SUCCESS -> context.getString(R.string.boot_status_finished_success)
            KIND_FINISHED_NOT_ROOT -> context.getString(R.string.boot_status_finished_not_root)
            KIND_FINISHED_SAFE_STOP -> context.getString(R.string.boot_status_finished_safe_stop)
            KIND_FINISHED_SKIPPED_UNSUPPORTED -> context.getString(R.string.boot_status_finished_skipped_unsupported)
            KIND_FINISHED_SKIPPED_CPU -> context.getString(R.string.boot_status_finished_skipped_cpu)
            KIND_FINISHED_SKIPPED_BUSY -> context.getString(R.string.boot_status_finished_skipped_busy)
            else -> context.getString(R.string.boot_last_status_empty)
        }
    }

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

    fun recordBootScheduled() {
        prefs.edit {
            putString(KEY_LAST_BOOT_KIND, KIND_SCHEDULED)
            remove(KEY_LAST_BOOT_EXTRA)
            putLong(KEY_LAST_BOOT_AT, System.currentTimeMillis())
        }
    }

    fun recordBootFailure(message: String) {
        prefs.edit {
            putString(KEY_LAST_BOOT_KIND, KIND_FAILED)
            putString(KEY_LAST_BOOT_EXTRA, message.take(120))
        }
    }

    fun recordBootSkipAlreadyScheduled() {
        prefs.edit {
            putString(KEY_LAST_BOOT_KIND, KIND_SKIP_ALREADY)
            remove(KEY_LAST_BOOT_EXTRA)
        }
    }

    fun recordBootSkipRunInProgress() {
        prefs.edit {
            putString(KEY_LAST_BOOT_KIND, KIND_SKIP_IN_PROGRESS)
            remove(KEY_LAST_BOOT_EXTRA)
        }
    }

    fun recordBootFinished(result: BootRunResult) {
        val kind = when (result) {
            BootRunResult.Success -> KIND_FINISHED_SUCCESS
            BootRunResult.FailedNotRoot -> KIND_FINISHED_NOT_ROOT
            is BootRunResult.StoppedSafely -> KIND_FINISHED_SAFE_STOP
            is BootRunResult.Skipped -> when (result.reason) {
                SkipReason.UnsupportedKernel -> KIND_FINISHED_SKIPPED_UNSUPPORTED
                SkipReason.NoCpuPair -> KIND_FINISHED_SKIPPED_CPU
                SkipReason.RunInProgress -> KIND_FINISHED_SKIPPED_BUSY
                SkipReason.Disabled -> return
            }
        }
        prefs.edit {
            putString(KEY_LAST_BOOT_KIND, kind)
            remove(KEY_LAST_BOOT_EXTRA)
        }
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
        const val KEY_LAST_BOOT_KIND = "last_boot_status_kind"
        const val KEY_LAST_BOOT_EXTRA = "last_boot_status_extra"
        const val KEY_LAST_BOOT_AT = "last_boot_at"
        const val KEY_LAST_BOOT_ELAPSED = "last_boot_elapsed"
        private const val KIND_SCHEDULED = "scheduled"
        private const val KIND_FAILED = "failed"
        private const val KIND_SKIP_ALREADY = "skip_already"
        private const val KIND_SKIP_IN_PROGRESS = "skip_in_progress"
        private const val KIND_FINISHED_SUCCESS = "finished_success"
        private const val KIND_FINISHED_NOT_ROOT = "finished_not_root"
        private const val KIND_FINISHED_SAFE_STOP = "finished_safe_stop"
        private const val KIND_FINISHED_SKIPPED_UNSUPPORTED = "finished_skipped_unsupported"
        private const val KIND_FINISHED_SKIPPED_CPU = "finished_skipped_cpu"
        private const val KIND_FINISHED_SKIPPED_BUSY = "finished_skipped_busy"
        const val DEFAULT_MAX_ATTEMPTS = 3
        const val MIN_ATTEMPTS = 1
        const val MAX_ATTEMPTS = 10
    }
}
