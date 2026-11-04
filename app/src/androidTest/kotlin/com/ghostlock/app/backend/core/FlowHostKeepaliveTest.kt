package com.ghostlock.app.backend.core

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.ghostlock.app.backend.diagnostics.DiagnosticKeepaliveFlow
import com.ghostlock.app.backend.diagnostics.DiagnosticOkFlow
import com.ghostlock.app.backend.launcher.AppProcessFlowLauncher
import com.ghostlock.app.core.flow.FlowResult
import com.ghostlock.app.backend.launcher.BackendExecutionState
import java.io.File
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/**
 * G11 设备 keepalive 门禁（本批只编译；真机由 Lead 下批执行）：
 *
 * DiagnosticKeepaliveFlow 返回 `host_keepalive=true` ⇒ host 终端跳过清理/自杀（A11），
 * 测试断言：Finished(Failed diag:keepalive)、evidence host_keepalive=true、
 * `host_run_dir_cleanup=skipped`、host pid 存活且 ≠ app pid；随后测试内用 `kill -9` 回收
 * （这是**测试清理**而非生产语义），再跑 DiagnosticOkFlow 断言新进程可用。
 *
 * @note 不依赖 fixture ELF、不跑真实攻击。
 */
@RunWith(AndroidJUnit4::class)
class FlowHostKeepaliveTest {
    private val context get() = InstrumentationRegistry.getInstrumentation().targetContext

    /** @return DiagnosticKeepaliveFlow 的登记 symbol。 */
    private fun keepaliveSymbol(): String = DiagnosticKeepaliveFlow::class.java.name + "#entry"

    /** @return DiagnosticOkFlow 的登记 symbol。 */
    private fun okSymbol(): String = DiagnosticOkFlow::class.java.name + "#entry"

    @Test
    fun keepaliveHostSurvivesUntilTestReclaimsAndNextRunUsesFreshProcess() = runBlocking {
        val launcher = AppProcessFlowLauncher(context)
        var hostPid = -1
        try {
            val state = launcher.run(keepaliveSymbol(), emptyMap(), null) { }
            assertTrue("expected Finished, got $state", state is BackendExecutionState.Finished)
            val result = (state as BackendExecutionState.Finished).result
            assertTrue("expected Failed, got $result", result is FlowResult.Failed)
            val failed = result as FlowResult.Failed
            assertEquals("diag:keepalive", failed.reason)
            assertEquals(
                "keepalive evidence missing: ${failed.evidence}",
                "true",
                failed.evidence["host_keepalive"],
            )
            assertEquals(
                "keepalive must skip run_dir cleanup: ${failed.evidence}",
                "skipped",
                failed.evidence["host_run_dir_cleanup"],
            )
            hostPid = launcher.lastHostPid
            assertTrue("host pid must be reported", hostPid > 0)
            assertNotEquals(
                "host must not be the app process",
                hostPid,
                android.os.Process.myPid(),
            )
            assertTrue(
                "keepalive host must still be alive after run returns (pid=$hostPid)",
                File("/proc/$hostPid").exists(),
            )
        } finally {
            /* A11 有意保留 host/run_dir；测试结束后回收，避免设备残留。 */
            if (hostPid > 0) {
                killHost(hostPid)
                awaitProcessGone(hostPid, OLD_PID_GONE_MS)
            }
        }
        assertTrue(
            "reclaimed host must be gone (pid=$hostPid)",
            !File("/proc/$hostPid").exists(),
        )

        val fresh = AppProcessFlowLauncher(context)
        val freshState = fresh.run(okSymbol(), emptyMap(), null) { }
        assertTrue("fresh run must finish: $freshState", freshState is BackendExecutionState.Finished)
        val freshResult = (freshState as BackendExecutionState.Finished).result
        assertTrue("fresh run must be Ok: $freshResult", freshResult is FlowResult.Ok)
        val freshPid = fresh.lastHostPid
        assertTrue("fresh run must report host pid", freshPid > 0)
        assertNotEquals("fresh run must use a new host process", hostPid, freshPid)
    }

    /**
     * 测试清理：同 uid 向 host 进程发 `kill -9`。
     *
     * @param pid 目标 host pid。
     */
    private fun killHost(pid: Int) {
        runCatching {
            Runtime.getRuntime()
                .exec(arrayOf("/system/bin/kill", "-9", pid.toString()))
                .waitFor()
        }
    }

    /**
     * 等待 `/proc/<pid>` 消失。
     *
     * @param pid 目标进程 pid。
     * @param timeoutMs 上限（ms）。
     * @return true=进程已消失。
     */
    private fun awaitProcessGone(pid: Int, timeoutMs: Long): Boolean {
        val deadline = System.currentTimeMillis() + timeoutMs
        while (System.currentTimeMillis() < deadline) {
            if (!File("/proc/$pid").exists()) return true
            Thread.sleep(POLL_MS)
        }
        return !File("/proc/$pid").exists()
    }

    private companion object {
        /** 旧 host pid 回收上限（ms）。 */
        const val OLD_PID_GONE_MS: Long = 5_000L

        /** /proc 轮询间隔（ms）。 */
        const val POLL_MS: Long = 50L
    }
}
