package com.ghostlock.app.autostart

import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.IBinder
import android.os.PowerManager
import android.os.SystemClock
import android.util.Log
import androidx.core.app.ServiceCompat
import androidx.core.content.ContextCompat
import com.ghostlock.app.R
import com.ghostlock.app.data.AndroidGhostlockRepository
import com.ghostlock.app.domain.model.ShizukuStatus
import com.ghostlock.app.domain.repository.GhostlockRepository
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.TimeoutCancellationException
import kotlinx.coroutines.async
import kotlinx.coroutines.cancel
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeout

/**
 * Headless boot auto-start runner.
 *
 * ACTION_RUN replays the manual Run flow without a UI: it resolves the same
 * snapshot (CPU pair, Shizuku choice, profile) and, when the Shizuku path is
 * selected, waits briefly for the daemon to come up after boot. When the run
 * completes — any exit code — the boot is marked as good [ConfirmationDelayMs]
 * later; a panic or reboot inside that window leaves the attempt unconfirmed
 * and the [BootGuard] counts it.
 *
 * ACTION_CONFIRM only arms the good-boot timer; the UI uses it after a manual
 * run so the guard is satisfied by any completed run, not just headless ones.
 */
class AutoStartService : Service() {
    private sealed interface RunOutcome {
        data class Completed(val code: Int, val logTail: List<String>) : RunOutcome
        data class Aborted(val reasonRes: Int) : RunOutcome
    }

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)
    private val guard by lazy { BootGuard.of(application) }
    private var repository: GhostlockRepository? = null
    private var runJob: Job? = null
    private var confirmJob: Job? = null
    private var wakeLock: PowerManager.WakeLock? = null

    override fun onCreate() {
        super.onCreate()
        AutostartNotifications.ensureChannels(this)
        wakeLock = (getSystemService(POWER_SERVICE) as PowerManager)
            .newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "ghostlock:autostart")
            .apply { acquire(WakeLockCapMs) }
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        val action = intent?.action
        val busy = when (action) {
            ACTION_RUN -> runJob?.isActive == true
            ACTION_CONFIRM -> runJob?.isActive == true || confirmJob != null
            else -> true
        }
        if (!busy) {
            val entered = when (action) {
                ACTION_RUN -> enterForeground(getString(R.string.autostart_running))
                ACTION_CONFIRM -> enterForeground(getString(R.string.autostart_confirming))
                else -> false
            }
            when {
                !entered -> stopSelf()
                action == ACTION_RUN -> startRun()
                else -> scheduleConfirmation(success = true, message = null)
            }
        }
        return START_NOT_STICKY
    }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onDestroy() {
        scope.cancel()
        repository?.close()
        wakeLock?.let { if (it.isHeld) it.release() }
        super.onDestroy()
    }

    private fun startRun() {
        // Everything repository-backed is disk-heavy: build and drive it on IO.
        val job = scope.launch(Dispatchers.IO) {
            val repo = repository ?: AndroidGhostlockRepository(this@AutoStartService)
                .also { repository = it }
            val outcome = try {
                runBootAttempt(repo)
            } catch (error: CancellationException) {
                throw error
            } catch (error: Exception) {
                Log.w(TAG, "headless run crashed: ${error.message}")
                RunOutcome.Aborted(R.string.autostart_abort_crashed)
            }
            when (outcome) {
                is RunOutcome.Completed -> finishRun(outcome)
                is RunOutcome.Aborted -> abortRun(outcome)
            }
            runJob = null
        }
        runJob = job
    }

    /** Maps a finished headless run onto the notification + confirmation timer. */
    private fun finishRun(outcome: RunOutcome.Completed) {
        val message = if (outcome.code == 0) {
            getString(R.string.autostart_completed)
        } else {
            getString(R.string.autostart_failed, outcome.code)
        }
        Log.i(TAG, "headless run finished exit=${outcome.code}")
        enterForeground(getString(R.string.autostart_confirming))
        scheduleConfirmation(
            success = outcome.code == 0,
            message = message,
            bigText = outcome.logTail.takeIf { outcome.code != 0 }?.joinToString("\n"),
        )
    }

    /** A run that never started cannot loop the boot; drop it without confirming. */
    private fun abortRun(outcome: RunOutcome.Aborted) {
        AutostartNotifications.notify(
            this,
            AutostartNotifications.ServiceNotificationId,
            AutostartNotifications.result(this, getString(outcome.reasonRes), null),
        )
        stopForeground(STOP_FOREGROUND_DETACH)
        runJob = null
        stopSelf()
    }

    private suspend fun runBootAttempt(repo: GhostlockRepository): RunOutcome {
        val logTail = ArrayDeque<String>()
        val onLog: (String) -> Unit = { line ->
            logTail.addLast(line)
            while (logTail.size > LogTailLines) logTail.removeFirst()
        }
        var snapshot = repo.snapshot()
        if (!snapshot.kernelSupported) {
            Log.w(TAG, "aborted: kernel not supported")
            return RunOutcome.Aborted(R.string.autostart_abort_unsupported)
        }
        if (snapshot.shizukuEnabled) {
            /* After a cold boot Shizuku comes up slightly behind BOOT_COMPLETED;
             * wait for it bounded, then give up (an aborted run risks nothing). */
            val deadline = SystemClock.elapsedRealtime() + ShizukuWaitMs
            while (snapshot.shizukuStatus != ShizukuStatus.READY) {
                if (SystemClock.elapsedRealtime() >= deadline) {
                    Log.w(TAG, "aborted: Shizuku not ready after ${ShizukuWaitMs / 1000}s")
                    return RunOutcome.Aborted(R.string.autostart_abort_shizuku)
                }
                delay(ShizukuPollMs)
                snapshot = repo.snapshot()
            }
        }
        val pair = snapshot.cpuPairs.getOrNull(snapshot.selectedCpuPair)
            ?: return RunOutcome.Aborted(R.string.autostart_abort_unsupported)
        Log.i(TAG, "headless run starting: path=${if (snapshot.shizukuEnabled) "shizuku" else "direct"} pair=$pair")
        val code = coroutineScope {
            val run = async {
                if (snapshot.shizukuEnabled) repo.runExploitWithShizuku(pair, onLog)
                else repo.runExploit(pair, onLog)
            }
            try {
                withTimeout(RunTimeoutMs) { run.await() }
            } catch (_: TimeoutCancellationException) {
                /* The native process does not observe coroutine cancellation on
                 * its own; close() destroyForciblys it so the IO thread unwinds,
                 * and the cancelled child completes deterministically. */
                withContext(NonCancellable) { runCatching { repo.close() } }
                ExitTimeout
            }
        }
        return RunOutcome.Completed(code, logTail.toList())
    }

    /** Keeps the FGS alive [ConfirmationDelayMs] before writing the good-boot mark. */
    private fun scheduleConfirmation(success: Boolean, message: String?, bigText: String? = null) {
        if (confirmJob != null) return
        val job = scope.launch {
            delay(ConfirmationDelayMs)
            guard.confirmCurrentBoot()
            Log.i(TAG, "good-boot mark written for this boot")
            if (success) {
                stopForeground(STOP_FOREGROUND_REMOVE)
            } else {
                AutostartNotifications.notify(
                    this@AutoStartService,
                    AutostartNotifications.ServiceNotificationId,
                    AutostartNotifications.result(this@AutoStartService, message.orEmpty(), bigText),
                )
                stopForeground(STOP_FOREGROUND_DETACH)
            }
            confirmJob = null
            stopSelf()
        }
        confirmJob = job
    }

    /** False when the platform refuses the promotion; the caller stops the service. */
    private fun enterForeground(message: String): Boolean = try {
        ServiceCompat.startForeground(
            this,
            AutostartNotifications.ServiceNotificationId,
            AutostartNotifications.foreground(this, message),
            ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE,
        )
        true
    } catch (error: Exception) {
        Log.w(TAG, "foreground promotion refused: ${error.message}")
        false
    }

    companion object {
        const val ACTION_RUN = "com.ghostlock.app.autostart.action.RUN"
        const val ACTION_CONFIRM = "com.ghostlock.app.autostart.action.CONFIRM"

        /**
         * Grace period between a completed run and the good-boot mark: a panic
         * or reboot inside this window leaves the attempt unconfirmed so the
         * [BootGuard] can disable auto-start.
         */
        const val ConfirmationDelayMs = 60_000L

        private const val TAG = "GhostLockAutostart"
        private const val ShizukuWaitMs = 30_000L
        private const val ShizukuPollMs = 2_000L
        private const val RunTimeoutMs = 5 * 60_000L
        private const val WakeLockCapMs = 10 * 60_000L
        private const val LogTailLines = 8
        private const val ExitTimeout = -1

        fun scheduleRun(context: Context) = start(context, ACTION_RUN)

        fun scheduleConfirmation(context: Context) = start(context, ACTION_CONFIRM)

        private fun start(context: Context, action: String) {
            val intent = Intent(context, AutoStartService::class.java).setAction(action)
            runCatching { ContextCompat.startForegroundService(context, intent) }
                .onFailure { Log.w(TAG, "$action dropped: ${it.message}") }
        }
    }
}
