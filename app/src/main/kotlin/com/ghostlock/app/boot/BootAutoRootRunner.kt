package com.ghostlock.app.boot

import android.content.Context
import com.ghostlock.app.data.AndroidGhostlockRepository
import com.ghostlock.app.data.BootAutoRootPreferences
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.delay

object BootAutoRootRunner {
    private const val BOOT_DELAY_MS = 15_000L
    private const val LOCKED_BOOT_DELAY_MS = 20_000L
    private const val RETRY_DELAY_MS = 5_000L

    suspend fun runIfConfigured(
        context: Context,
        beforeUnlock: Boolean,
        onLog: (String) -> Unit,
    ): BootRunResult {
        return try {
            runConfigured(context, beforeUnlock, onLog)
        } catch (error: CancellationException) {
            throw error
        } catch (error: Throwable) {
            val detail = error.message?.take(120) ?: error.javaClass.simpleName
            onLog("auto-run stopped safely: $detail")
            BootRunResult.StoppedSafely(detail)
        }
    }

    private suspend fun runConfigured(
        context: Context,
        beforeUnlock: Boolean,
        onLog: (String) -> Unit,
    ): BootRunResult {
        val prefs = BootAutoRootPreferences(context)
        if (!prefs.autoRunAtBoot) {
            onLog("auto run at boot is disabled")
            return BootRunResult.Skipped(SkipReason.Disabled)
        }
        prefs.syncRuntimeFromApp()
        val delayMs = if (beforeUnlock) LOCKED_BOOT_DELAY_MS else BOOT_DELAY_MS
        onLog(
            if (beforeUnlock) {
                "waiting ${delayMs / 1000}s (direct boot, lock screen)"
            } else {
                "waiting ${delayMs / 1000}s for boot to settle"
            },
        )
        delay(delayMs)
        val storageContext = if (beforeUnlock) {
            context.applicationContext.createDeviceProtectedStorageContext()
        } else {
            context.applicationContext
        }
        val repository = try {
            AndroidGhostlockRepository(storageContext)
        } catch (error: Throwable) {
            val detail = error.message?.take(120) ?: error.javaClass.simpleName
            onLog("could not prepare boot run: $detail")
            return BootRunResult.StoppedSafely(detail)
        }
        try {
            val snapshot = try {
                repository.snapshot()
            } catch (error: Throwable) {
                val detail = error.message?.take(120) ?: error.javaClass.simpleName
                onLog("skip: could not read device info ($detail)")
                return BootRunResult.StoppedSafely(detail)
            }
            if (!snapshot.kernelSupported) {
                onLog("skip: kernel unsupported (${snapshot.kernelRelease})")
                return BootRunResult.Skipped(SkipReason.UnsupportedKernel)
            }
            val pair = snapshot.cpuPairs.getOrNull(snapshot.selectedCpuPair)
            if (pair == null) {
                onLog("skip: no CPU pair selected")
                return BootRunResult.Skipped(SkipReason.NoCpuPair)
            }
            val maxAttempts = prefs.maxAttempts
            onLog("cpu pair: ${snapshot.cpuPairLabels.getOrElse(snapshot.selectedCpuPair) { pair.toString() }}")
            repeat(maxAttempts) { index ->
                val attempt = index + 1
                onLog("==== boot attempt $attempt/$maxAttempts ====")
                val code = try {
                    repository.runExploit(pair, onLog)
                } catch (error: Throwable) {
                    val detail = error.message?.take(120) ?: error.javaClass.simpleName
                    onLog("attempt $attempt stopped safely: $detail")
                    -1
                }
                if (code == 0) {
                    onLog("result: exploit completed on attempt $attempt")
                    return BootRunResult.Success
                }
                onLog("result: exploit failed (exit code=$code)")
                if (attempt < maxAttempts) {
                    onLog("retrying…")
                    delay(RETRY_DELAY_MS)
                }
            }
            onLog("result: not rooted after $maxAttempts attempt(s)")
            return BootRunResult.FailedNotRoot
        } finally {
            try {
                repository.close()
            } catch (_: Throwable) {
            }
        }
    }
}
