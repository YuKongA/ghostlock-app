package com.ghostlock.app.backend.core

import android.content.pm.PackageManager
import android.util.Log
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.ghostlock.app.BuildConfig
import com.ghostlock.app.backend.backends.debugprobe.BackendDebugProbe
import com.ghostlock.app.backend.launcher.ShizukuProcessFlowLauncher
import com.ghostlock.app.core.flow.FlowResult
import com.ghostlock.app.ui.backend.BackendRunDispatch
import com.ghostlock.app.ui.backend.BackendRunDispatcher
import com.ghostlock.app.ui.backend.BackendRunGate
import com.ghostlock.app.ui.backend.BackendRunUiState
import com.ghostlock.app.ui.backend.RunPageContext
import com.ghostlock.app.ui.screen.home.ExecutorMode
import com.ghostlock.app.ui.screen.home.ExecutorPreference
import java.io.File
import java.util.Collections
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith
import rikka.shizuku.Shizuku

/**
 * Debug Probe 设备门禁（debug APK；不跑真实 ELF/攻击）：
 *
 * - App `:flowhost` 分支：经生产 dispatcher 运行 [BackendDebugProbe]，断言
 *   Finished.Ok / 输出 marker / host pid≠app / `host_log_path` 落盘（存在、非空、含 `debug-probe:done`）。
 * - Shizuku 分支（若可用）：同一 probe 经 [ShizukuProcessFlowLauncher]，
 *   断言 host_uid=2000、host_domain 含 shell、输出 marker；`host_log_path` 不可读时记录跳过原因不失败。
 *
 * @note 仅只读环境探测；无攻击副作用。
 */
@RunWith(AndroidJUnit4::class)
class DebugProbeHostSmokeTest {
    private val context get() = InstrumentationRegistry.getInstrumentation().targetContext

    /** probe entry 的登记 symbol（debug registry 自动包含）。 */
    private val probeSymbol: String =
        "com.ghostlock.app.backend.backends.debugprobe.FlowDebugProbeEntry#entry"

    @Test
    fun appHostRunsDebugProbeAndPersistsLog(): Unit = runBlocking {
        assumeTrue("debug probe is debug-build only", BuildConfig.DEBUG)
        /* 设备可能持久化了 Shizuku 执行体；App 分支必须强制 App host（persist 在 filesDir 下）。 */
        ExecutorPreference.load(context)
        val previousMode = ExecutorPreference.mode
        ExecutorPreference.set(context, ExecutorMode.App)
        try {
        val output = Collections.synchronizedList(mutableListOf<String>())
        val spec = BackendDebugProbe
        val decision = BackendRunGate.decide(spec, spec.readiness(), RunPageContext())
        assertTrue("gate rejected debug probe: ${decision.reasons}", decision.allowed)

        val dispatch = BackendRunDispatcher.dispatch(spec, decision, context) { line ->
            output += line
        }
        assertTrue("dispatcher did not execute: $dispatch", dispatch is BackendRunDispatch.Executed)
        val state = (dispatch as BackendRunDispatch.Executed).state
        assertTrue("expected Finished, got $state", state is BackendRunUiState.Finished)
        val result = (state as BackendRunUiState.Finished).result
        assertTrue("expected Ok, got $result", result is FlowResult.Ok)
        val evidence = (result as FlowResult.Ok).evidence
        assertEquals("ok", evidence["debug_probe"])

        assertTrue(output.any { it.startsWith("debug-probe:host-uid=") })
        assertTrue(output.any { it == "debug-probe:line:0" })
        assertTrue(output.any { it == "debug-probe:line:127" })
        assertTrue(output.any { it == "debug-probe:done" })
        assertTrue("probe lines missing", output.size >= PROBE_LINE_COUNT)

        val hostPid = evidence["host_pid"]
        assertTrue("host pid missing", hostPid != null && hostPid != "0")
        assertNotEquals(
            "host must not be app process",
            hostPid,
            android.os.Process.myPid().toString(),
        )
        assertTrue("host_run_dir missing", !evidence["host_run_dir"].isNullOrBlank())

        /* host 终态会 cleanup run_dir ⇒ 「落盘」主判据用 run_dir 外的 persist_path。 */
        val persistPath = evidence["debug_probe.persist_path"].orEmpty()
        assertTrue(
            "debug_probe.persist_path missing: $persistPath",
            persistPath.isNotBlank() && persistPath != "<absent>",
        )
        val persistFile = File(persistPath)
        assertTrue("persist log must exist: $persistPath", persistFile.isFile)
        assertTrue("persist log must not be empty: $persistPath", persistFile.length() > 0)
        val persistText = persistFile.readText()
        assertTrue("persist log must contain done marker", persistText.contains("debug-probe:done"))
        assertTrue("persist log must contain line markers", persistText.contains("debug-probe:line:127"))
        assertTrue("persist log must contain locale", persistText.contains("debug-probe:locale="))
        assertTrue("persist log must contain cpu-online", persistText.contains("debug-probe:cpu-online="))
        assertTrue("persist log must contain meminfo", persistText.contains("debug-probe:meminfo:0="))
        val persistBytes = evidence["persist_bytes"]?.toIntOrNull() ?: 0
        assertTrue("persist_bytes must be > 0: $persistBytes", persistBytes > 0)
        assertTrue(
            "persist_path must live under filesDir: $persistPath",
            persistPath.startsWith(context.filesDir.absolutePath),
        )

        /* host.log_path 在 run_dir 内，cleanup 后通常已删除：存在则附加断言，不存在不失败。 */
        val logPath = evidence["host_log_path"].orEmpty()
        if (logPath.isNotBlank()) {
            val logFile = File(logPath)
            if (logFile.isFile && logFile.length() > 0) {
                assertTrue(
                    "host.log_path exists but missing done marker: $logPath",
                    logFile.readText().contains("debug-probe:done"),
                )
            } else {
                Log.i(TAG, "host.log_path removed by run_dir cleanup (expected): $logPath")
            }
        }
        /* 第二次 run：新 host 进程；persist 覆盖写后仍存在且含 done。 */
        val secondDispatch = BackendRunDispatcher.dispatch(spec, decision, context) { }
        assertTrue(
            "second dispatcher did not execute: $secondDispatch",
            secondDispatch is BackendRunDispatch.Executed,
        )
        val secondState = (secondDispatch as BackendRunDispatch.Executed).state
        assertTrue("second run must finish: $secondState", secondState is BackendRunUiState.Finished)
        val secondResult = (secondState as BackendRunUiState.Finished).result
        assertTrue("second run must be Ok: $secondResult", secondResult is FlowResult.Ok)
        val secondEvidence = (secondResult as FlowResult.Ok).evidence
        val secondPid = secondEvidence["host_pid"]
        assertNotEquals("every probe run must use a new host process", hostPid, secondPid)
        val secondPersistPath = secondEvidence["debug_probe.persist_path"].orEmpty()
        assertEquals("persist path must be stable across runs", persistPath, secondPersistPath)
        val secondPersistFile = File(secondPersistPath)
        assertTrue("persist file must still exist after rerun: $secondPersistPath", secondPersistFile.isFile)
        assertTrue(
            "persist file must still contain done after rerun",
            secondPersistFile.readText().contains("debug-probe:done"),
        )
        Log.i(
            TAG,
            "app probe ok pid=$hostPid rerunPid=$secondPid persist=$persistPath lines=${output.size}",
        )
        Unit
        } finally {
            runCatching { ExecutorPreference.set(context, previousMode) }
        }
    }

    @Test
    fun shizukuHostRunsDebugProbeWhenAvailable(): Unit = runBlocking {
        assumeTrue("debug probe is debug-build only", BuildConfig.DEBUG)
        val probe = runCatching {
            val binder = Shizuku.pingBinder()
            val permission = Shizuku.checkSelfPermission() == PackageManager.PERMISSION_GRANTED
            val uid = Shizuku.getUid()
            val domain = Shizuku.getSELinuxContext()
            "binder=$binder permission=$permission uid=$uid domain=$domain" to
                (binder && permission && uid == 2000 && domain?.contains("u:r:shell:s0") == true)
        }.getOrElse { problem -> ("probeError=" + problem::class.java.simpleName) to false }
        assumeTrue("Shizuku unavailable: ${probe.first}", probe.second)

        val output = Collections.synchronizedList(mutableListOf<String>())
        val launcher = ShizukuProcessFlowLauncher(context)
        val state = launcher.run(probeSymbol, emptyMap(), null) { line ->
            output += line
        }
        assertTrue("expected Finished, got $state", state is BackendRunUiState.Finished)
        val result = (state as BackendRunUiState.Finished).result
        assertTrue("expected Ok, got $result", result is FlowResult.Ok)
        val evidence = (result as FlowResult.Ok).evidence
        assertEquals("2000", evidence["host_uid"])
        assertTrue(
            "host_domain must be shell: ${evidence["host_domain"]}",
            evidence["host_domain"]?.contains("u:r:shell:s0") == true,
        )
        assertTrue(output.any { it == "debug-probe:done" })
        assertTrue(output.any { it.startsWith("debug-probe:env:") || it.startsWith("debug-probe:prop:") })
        val hostPid = launcher.lastHostPid
        assertTrue("shell host pid must be reported", hostPid > 0)

        /* shell 路径属别的 uid，可能 isFile/length 可查但 readText 抛 EACCES ⇒ 全部 runCatching 兜底。 */
        val persistPath = evidence["debug_probe.persist_path"].orEmpty()
        val persistText = runCatching { File(persistPath).readText() }.getOrNull()
        if (persistPath.isNotBlank() && persistPath != "<absent>" && !persistText.isNullOrEmpty()) {
            assertTrue(
                "shell persist log must contain done marker: $persistPath",
                persistText.contains("debug-probe:done"),
            )
        } else {
            val logPath = evidence["host_log_path"].orEmpty()
            val logText = runCatching { File(logPath).readText() }.getOrNull()
            if (logPath.isNotBlank() && !logText.isNullOrEmpty()) {
                assertTrue(
                    "shell log file must contain done marker: $logPath",
                    logText.contains("debug-probe:done"),
                )
            } else {
                Log.w(
                    TAG,
                    "shell persist/log path unreadable (EACCES?) — skip persistence assert: " +
                        "persist=$persistPath log=$logPath",
                )
            }
        }
        Log.i(TAG, "shizuku probe ok pid=$hostPid persist=$persistPath lines=${output.size}")
        Unit
    }

    private companion object {
        /** logcat TAG。 */
        const val TAG = "GLK-DEBUG-PROBE"

        /** 固定探针行数（debug-probe:line:0..127）。 */
        const val PROBE_LINE_COUNT = 128
    }
}
