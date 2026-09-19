package com.ghostlock.app.boot

import android.content.Context
import com.ghostlock.app.data.AndroidGhostlockRepository
import com.ghostlock.app.data.BootAutoRootPreferences
import com.ghostlock.app.boot.ExploitRunLock
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.delay

object BootAutoRootRunner {
    private const val BOOT_DELAY_MS = 15_000L
    private const val RETRY_DELAY_MS = 5_000L

    suspend fun runIfConfigured(
        context: Context,
        onLog: (String) -> Unit,
    ): BootRunResult {
        return try {
            runConfigured(context, onLog)
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
        onLog: (String) -> Unit,
    ): BootRunResult {
        val prefs = BootAutoRootPreferences(context)
        if (!prefs.autoRunAtBoot) {
            onLog("auto run at boot is disabled")
            return BootRunResult.Skipped(SkipReason.Disabled)
        }
        onLog("waiting ${BOOT_DELAY_MS / 1000}s after unlock for boot to settle")
        delay(BOOT_DELAY_MS)
        val repository = try {
            AndroidGhostlockRepository(context.applicationContext)
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
                if (code == ExploitRunLock.EXIT_BUSY) {
                    onLog("skip: another run is in progress")
                    return BootRunResult.Skipped(SkipReason.RunInProgress)
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
