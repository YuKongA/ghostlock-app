package com.ghostlock.app.backend.core

import android.content.pm.PackageManager
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.ghostlock.app.backend.diagnostics.DiagnosticHandoffFlow
import com.ghostlock.app.backend.diagnostics.DiagnosticOkFlow
import com.ghostlock.app.backend.host.FlowHostProtocol
import com.ghostlock.app.backend.launcher.ShizukuProcessFlowLauncher
import com.ghostlock.app.core.flow.FlowResult
import com.ghostlock.app.backend.launcher.BackendExecutionState
import java.io.File
import java.util.Collections
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import rikka.shizuku.Shizuku

/**
 * B3a/B3b：Shizuku 进程 FlowHost smoke（本批只编译；B3b 设备门禁由 Lead 执行）。
 *
 * A301SO 无 Shizuku 包 ⇒ [assumeTrue] 使全部用例 skip 并记录 reason；若设备可用则断言
 * DiagnosticOkFlow 3 行 + Finished.Ok、两次 run 新 pid/旧进程消失、host_uid=2000、
 * host_domain 含 u:r:shell:s0、非法 symbol/payload 拒绝、DiagnosticHandoffFlow→HandoffRequested。
 *
 * @note 只使用公开 API；不启动真实攻击/文件写。
 */
@RunWith(AndroidJUnit4::class)
class ShizukuFlowHostSmokeTest {
    private val context get() = InstrumentationRegistry.getInstrumentation().targetContext

    /** 无 Shizuku（未运行/未授权/非 shell uid/domain）时 skip 并留 reason。 */
    @Before
    fun requireShizuku() {
        assumeTrue(
            "flow-launcher:shizuku:preflight:not-available on this device",
            shizukuAvailable(),
        )
    }

    /** @return true=Shizuku binder+权限+uid 2000+shell domain 全部满足。 */
    private fun shizukuAvailable(): Boolean = runCatching {
        Shizuku.pingBinder() &&
            Shizuku.checkSelfPermission() == PackageManager.PERMISSION_GRANTED &&
            Shizuku.getUid() == 2000 &&
            Shizuku.getSELinuxContext()?.contains("u:r:shell:s0") == true
    }.getOrDefault(false)

    /** @return DiagnosticOkFlow 的登记 symbol。 */
    private fun okSymbol(): String = DiagnosticOkFlow::class.java.name + "#entry"

    /** @return DiagnosticHandoffFlow 的登记 symbol。 */
    private fun handoffSymbol(): String = DiagnosticHandoffFlow::class.java.name + "#entry"

    @Test
    fun diagnosticOkRunsInFreshShellProcess() = runBlocking {
        val lines = Collections.synchronizedList(mutableListOf<String>())
        val first = ShizukuProcessFlowLauncher(context)
        val state = first.run(okSymbol(), mapOf("k" to "v"), null) { line -> lines += line }
        assertTrue("expected Finished, got $state", state is BackendExecutionState.Finished)
        val result = (state as BackendExecutionState.Finished).result
        assertTrue(result is FlowResult.Ok)
        assertEquals(3, lines.count { it.startsWith("diag:line:") })
        assertEquals("2000", result.evidence["host_uid"])
        assertTrue(result.evidence["host_domain"]?.contains("u:r:shell:s0") == true)
        val firstPid = first.lastHostPid
        assertTrue("shell host pid must be reported", firstPid > 0)
        val deadline = System.currentTimeMillis() + 5_000L
        while (System.currentTimeMillis() < deadline && File("/proc/$firstPid").exists()) {
            Thread.sleep(50L)
        }
        val second = ShizukuProcessFlowLauncher(context)
        second.run(okSymbol(), emptyMap(), null) { }
        val secondPid = second.lastHostPid
        assertNotEquals("every run must use a new shell process", firstPid, secondPid)
        assertFalse("old shell process must disappear", File("/proc/$firstPid").exists())
    }

    @Test
    fun invalidSymbolIsRefused() = runBlocking {
        val state = ShizukuProcessFlowLauncher(context)
            .run("com.ghostlock.app.core.flow.NoSuchFlow#entry", emptyMap(), null) { }
        assertTrue(state is BackendExecutionState.Refused)
        val reasons = (state as BackendExecutionState.Refused).reasons
        assertTrue(reasons.any { it.contains("symbol", ignoreCase = true) })
    }

    @Test
    fun oversizePayloadIsRefused() = runBlocking {
        val payload = ByteArray(FlowHostProtocol.MAX_PAYLOAD_BYTES + 1)
        val state = ShizukuProcessFlowLauncher(context)
            .run(okSymbol(), emptyMap(), payload) { }
        assertTrue(state is BackendExecutionState.Refused)
        val reasons = (state as BackendExecutionState.Refused).reasons
        assertTrue(reasons.any { it.contains("payload", ignoreCase = true) })
    }

    @Test
    fun handoffIsNotReportedAsSuccess() = runBlocking {
        val state = ShizukuProcessFlowLauncher(context)
            .run(handoffSymbol(), emptyMap(), null) { }
        assertEquals(BackendExecutionState.HandoffRequested, state)
    }
}
