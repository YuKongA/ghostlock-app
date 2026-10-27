package com.ghostlock.app.boot

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.ComponentName
import android.content.pm.PackageManager
import android.os.PowerManager
import android.util.Log
import com.ghostlock.app.GhostlockApplication
import com.ghostlock.app.domain.model.ShizukuStatus
import com.ghostlock.app.domain.repository.GhostlockRepository
import kotlinx.coroutines.delay
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeoutOrNull

/** Runs autoroot directly from the boot receiver, matching DirtyFrag's model. */
class BootCompletedReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        if (!isBootAction(intent.action)) return
        when (AutorootBootGuard.beginBoot(context)) {
            AutorootBootGuard.Decision.DUPLICATE -> {
                Log.i(TAG, "boot: duplicate broadcast ignored")
                return
            }
            AutorootBootGuard.Decision.TRIPPED -> {
                Log.i(TAG, "boot: one unconfirmed boot; disabling autoroot")
                disableAutorun(context)
                return
            }
            AutorootBootGuard.Decision.RUN -> Unit
        }
        val deContext = context.createDeviceProtectedStorageContext()
        val power = context.getSystemService(PowerManager::class.java) ?: return
        val wakeLock = power.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "$TAG:boot")
        wakeLock.acquire()
        Thread {
            var ok = false
            val repository = (deContext.applicationContext as? GhostlockApplication)
                ?.createRepository(deContext)
            try {
                if (repository == null) return@Thread
                val snapshot = runBlocking { repository.snapshot() }
                val pair = snapshot.cpuPairs.getOrNull(snapshot.selectedCpuPair)
                if (!snapshot.kernelSupported || pair == null) return@Thread
                val code = runBlocking {
                    val ready = !snapshot.shizukuEnabled || waitForShizuku(repository)
                    if (!ready) SHIZUKU_TIMEOUT_EXIT
                    else if (snapshot.shizukuEnabled) repository.runExploitWithShizuku(pair) { line -> Log.i(TAG, line.trim()) }
                    else repository.runExploit(pair) { line -> Log.i(TAG, line.trim()) }
                }
                Log.i(TAG, "boot: exploit rc=$code")
                ok = code == 0
                if (ok) AutorootBootGuard.confirmBoot(context)
            } catch (error: Exception) {
                Log.e(TAG, "boot: exploit exception", error)
            } finally {
                repository?.close()
                wakeLock.release()
            }
            if (!ok) disableAutorun(context)
        }.start()
    }

    private suspend fun waitForShizuku(repository: GhostlockRepository): Boolean {
        var attempt = 0
        val ready: Boolean = withTimeoutOrNull<Boolean>(SHIZUKU_TIMEOUT_MS) {
            var available = false
            while (!available) {
                val status = repository.snapshot().shizukuStatus
                attempt++
                Log.i(TAG, "boot: Shizuku check $attempt status=$status")
                available = status == ShizukuStatus.READY
                if (!available) delay(SHIZUKU_POLL_INTERVAL_MS)
            }
            available
        } ?: false
        if (!ready) Log.i(TAG, "boot: Shizuku timeout after ${SHIZUKU_TIMEOUT_MS}ms")
        return ready
    }

    private fun disableAutorun(context: Context) {
        Log.i(TAG, "boot: failed - disabling autorun interlock")
        context.packageManager.setComponentEnabledSetting(
            ComponentName(context, BootCompletedReceiver::class.java),
            PackageManager.COMPONENT_ENABLED_STATE_DISABLED,
            PackageManager.DONT_KILL_APP,
        )
    }

    private companion object {
        const val TAG = "GhostLock"
        const val SHIZUKU_POLL_INTERVAL_MS = 2_000L
        const val SHIZUKU_TIMEOUT_MS = 60_000L
        const val SHIZUKU_TIMEOUT_EXIT = 124
    }
}

internal fun isBootAction(action: String?): Boolean =
    action == Intent.ACTION_BOOT_COMPLETED || action == Intent.ACTION_LOCKED_BOOT_COMPLETED
