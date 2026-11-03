package com.ghostlock.app.backend.core

import android.util.Log
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.ghostlock.app.backend.backends.cve43499glued.BackendCve43499Glued
import com.ghostlock.app.backend.backends.cve43499glued.Glued43499ProfileCodec
import com.ghostlock.app.core.flow.FlowResult
import com.ghostlock.app.ui.backend.BackendConfigStore
import com.ghostlock.app.ui.backend.BackendRunDispatch
import com.ghostlock.app.ui.backend.BackendRunDispatcher
import com.ghostlock.app.ui.backend.BackendRunGate
import com.ghostlock.app.ui.backend.BackendRunUiState
import com.ghostlock.app.ui.backend.RunPageContext
import java.io.File
import java.util.Collections
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith

/**
 * **B4-real（G-43499-real）唯一真实攻击入口**：在生产路径上执行一次 43499-Glued 真实攻击。
 *
 * 用户 2026-11-02「允许授权」：允许在 A301SO（历史测试机口径）执行真实漏洞门禁；
 * 允许 panic/重启，失败即停，**不得重跑**。
 *
 * 链路：`BackendRunDispatcher.dispatch` → `FlowLaunchParent` → `:flowhost` → 生产
 * `Flow43499GluedEntry` → 真实 `lib43499-Glued.so`（非 no-op fixture）。
 *
 * 安全/门禁约束：
 * - **opt-in**：必须传 instrumentation 参数 `-e ghostlock_real_43499 1`，否则 skip；
 * - `lib43499-Glued.so` 若含 no-op fixture marker（`fixture=ghostlock-exec-noop-v1`）则 skip，
 *   防止 fixture/production 混淆下误跑；
 * - 只调用一次生产 dispatch；**不要求攻击成功**（返回 Failed/Refused 也记录，panic/重启由设备门禁判）；
 * - 本测试不新增/修改生产代码，只作入口与证据记录。
 */
@RunWith(AndroidJUnit4::class)
class Real43499GateTest {
    private val context get() = InstrumentationRegistry.getInstrumentation().targetContext

    @Test
    fun real43499AttackThroughProductionChannel() = runBlocking {
        val optIn = InstrumentationRegistry.getArguments().getString(ARG_OPT_IN)
        assumeTrue(
            "real 43499 gate is opt-in; pass -e $ARG_OPT_IN 1",
            optIn == "1",
        )

        val binary = File(context.applicationInfo.nativeLibraryDir, BINARY_NAME)
        assertTrue("production $BINARY_NAME missing: $binary", binary.isFile)
        val binaryText = binary.readBytes().toString(Charsets.ISO_8859_1)
        assumeTrue(
            "no-op fixture ELF detected; refusing real attack (production ELF required)",
            !binaryText.contains(NOOP_FIXTURE_MARKER),
        )

        for (line in environmentSnapshot()) {
            Log.i(TAG, line)
        }

        val osVersion = System.getProperty("os.version").orEmpty()
        val builtins = Glued43499ProfileCodec.builtins(context)
        val releases = builtins.map { it.release }
        val matched = builtins.firstOrNull { asset ->
            osVersion.contains(asset.release, ignoreCase = true)
        } ?: builtins.firstOrNull { asset ->
            osVersion.isNotBlank() && asset.release.contains(osVersion, ignoreCase = true)
        }
        assertNotNull(
            "no builtin profile matches os.version=$osVersion; candidates=$releases",
            matched,
        )
        val release = requireNotNull(matched).release
        val spec = BackendCve43499Glued
        val store = BackendConfigStore.open(context, spec.cve, spec.configSchema)
        spec.selectProfile("builtin:$release", store)
        Log.i(TAG, "profile.selected=builtin:$release os.version=$osVersion")

        val decision = BackendRunGate.decide(spec, spec.readiness(), RunPageContext())
        assertTrue(
            "gate denied real 43499 attack: ${decision.reasons}",
            decision.allowed,
        )
        Log.i(TAG, "gate.allowed=true")

        val output = Collections.synchronizedList(mutableListOf<String>())
        val startedAt = System.currentTimeMillis()
        val dispatch = BackendRunDispatcher.dispatch(spec, decision, context) { line ->
            output += line
        }
        val elapsedMs = System.currentTimeMillis() - startedAt
        assertTrue(
            "dispatcher did not execute the production channel: $dispatch",
            dispatch is BackendRunDispatch.Executed,
        )
        val state = (dispatch as BackendRunDispatch.Executed).state
        Log.i(TAG, "terminal.state=$state elapsedMs=$elapsedMs")
        when (state) {
            is BackendRunUiState.Finished -> when (val result = state.result) {
                is FlowResult.Ok -> Log.i(
                    TAG,
                    "result=Ok steps=${result.steps.size} evidence=${result.evidence}",
                )

                is FlowResult.Failed -> Log.e(
                    TAG,
                    "result=Failed reason=${result.reason} failedStep=${result.failedStep} " +
                        "steps=${result.steps.size} evidence=${result.evidence}",
                )
            }

            is BackendRunUiState.Refused -> Log.e(
                TAG,
                "result=Refused reasons=${state.reasons}",
            )

            BackendRunUiState.HandoffRequested -> Log.i(TAG, "result=HandoffRequested")
            BackendRunUiState.Idle -> Log.i(TAG, "result=Idle")
            BackendRunUiState.Running -> Log.i(TAG, "result=Running")
        }
        output.takeLast(MAX_LOG_LINES).forEachIndexed { index, line ->
            Log.i(TAG, "out[$index]=$line")
        }
        Log.i(TAG, "b4-real.done elapsedMs=$elapsedMs outputLines=${output.size}")
    }

    /** @return 冷机/环境快照（仅记录，不影响判据）。 */
    private fun environmentSnapshot(): List<String> = listOf(
        "snapshot.uptime=" + readFile("/proc/uptime"),
        "snapshot.selinux_enforce=" + readFile("/sys/fs/selinux/enforce"),
        "snapshot.kernelsu_module_path_exists=" + File("/sys/module/kernelsu").exists(),
        "snapshot.boot_id=" + readFile("/proc/sys/kernel/random/boot_id"),
        "snapshot.os_version=" + System.getProperty("os.version").orEmpty(),
        "snapshot.build_display=" + android.os.Build.DISPLAY,
        "snapshot.selinux_context=" + readFile("/proc/self/attr/current"),
        "snapshot.app_pid=" + android.os.Process.myPid(),
    )

    /** @return 文件文本（去空白）；不可读返回 "unreadable"。 */
    private fun readFile(path: String): String = runCatching {
        File(path).readText().trim { it == '\u0000' || it.isWhitespace() }
    }.getOrDefault("unreadable")

    private companion object {
        /** opt-in instrumentation 参数名。 */
        const val ARG_OPT_IN = "ghostlock_real_43499"

        /** no-op fixture marker（命中即拒绝真实攻击）。 */
        const val NOOP_FIXTURE_MARKER = "fixture=ghostlock-exec-noop-v1"

        /** 生产 43499 ELF 名。 */
        const val BINARY_NAME = "lib43499-Glued.so"

        /** logcat TAG。 */
        const val TAG = "GLK-REAL43499"

        /** 输出尾行记录上限。 */
        const val MAX_LOG_LINES = 80
    }
}
