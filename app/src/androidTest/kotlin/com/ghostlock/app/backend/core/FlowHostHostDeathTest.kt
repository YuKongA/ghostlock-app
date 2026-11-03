package com.ghostlock.app.backend.core

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.ghostlock.app.backend.diagnostics.DiagnosticOkFlow
import com.ghostlock.app.backend.diagnostics.DiagnosticSlowFlow
import com.ghostlock.app.backend.launcher.AppProcessFlowLauncher
import com.ghostlock.app.core.flow.FlowResult
import com.ghostlock.app.ui.backend.BackendRunUiState
import java.io.File
import java.util.Collections
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.cancelAndJoin
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/**
 * G14 设备门禁（本批只编译；真机由 Lead 下批执行）：
 * 运行中 kill `:flowhost` 进程 ⇒ launcher 快速返回 Refused(`flow-launcher:app:host-lost`)，
 * 不等满 60s（≤[HOST_DEATH_TIMEOUT_MS]）；旧 pid `/proc` 消失；随后 fresh Ok flow 用新进程。
 *
 * @note 只使用诊断 fixture（DiagnosticSlowFlow/DiagnosticOkFlow）；不跑真实攻击。
 */
@RunWith(AndroidJUnit4::class)
class FlowHostHostDeathTest {
    private val context get() = InstrumentationRegistry.getInstrumentation().targetContext

    /** @return DiagnosticSlowFlow 的登记 symbol。 */
    private fun slowSymbol(): String = DiagnosticSlowFlow::class.java.name + "#entry"

    /** @return DiagnosticOkFlow 的登记 symbol。 */
    private fun okSymbol(): String = DiagnosticOkFlow::class.java.name + "#entry"

    @Test
    fun hostDeathWhileRunningFailsFastWithHostLostAndNextRunUsesFreshProcess() = runBlocking {
        val ticks = Collections.synchronizedList(mutableListOf<String>())
        val launcher = AppProcessFlowLauncher(context)
        var result: BackendRunUiState? = null
        val job = launch(Dispatchers.Default) {
            result = launcher.run(slowSymbol(), emptyMap(), null) { line -> ticks += line }
        }
        var hostPid = -1
        try {
            waitForFirstTick(ticks)
            hostPid = waitForHostPid(launcher)
            assertTrue("host pid must be reported", hostPid > 0)
            assertNotEquals(
                "host must not be app process",
                hostPid,
                android.os.Process.myPid(),
            )
            assertTrue("host must be alive before kill", File("/proc/$hostPid").exists())

            killPid(hostPid)
            val killedAt = System.currentTimeMillis()
            withTimeout(HOST_DEATH_TIMEOUT_MS) { job.join() }
            val elapsed = System.currentTimeMillis() - killedAt
            assertTrue(
                "host-lost must fail fast (took ${elapsed}ms, limit ${HOST_DEATH_TIMEOUT_MS}ms)",
                elapsed <= HOST_DEATH_TIMEOUT_MS,
            )
            val state = result
            assertTrue("expected Refused, got $state", state is BackendRunUiState.Refused)
            val reasons = (state as BackendRunUiState.Refused).reasons
            assertTrue(
                "host-lost reason missing: $reasons",
                reasons.any { it.contains("flow-launcher:app:host-lost") },
            )
            assertTrue(
                "old host pid must disappear (pid=$hostPid)",
                awaitGone(hostPid, OLD_PID_GONE_MS),
            )
        } finally {
            if (job.isActive) job.cancelAndJoin()
            if (hostPid > 0 && File("/proc/$hostPid").exists()) {
                killPid(hostPid)
                awaitGone(hostPid, OLD_PID_GONE_MS)
            }
        }

        val fresh = AppProcessFlowLauncher(context)
        val freshState = fresh.run(okSymbol(), emptyMap(), null) { }
        assertTrue(
            "fresh run must finish: $freshState",
            freshState is BackendRunUiState.Finished,
        )
        val freshResult = (freshState as BackendRunUiState.Finished).result
        assertTrue("fresh run must be Ok: $freshResult", freshResult is FlowResult.Ok)
        val freshPid = fresh.lastHostPid
        assertTrue("fresh run must report host pid", freshPid > 0)
        assertNotEquals("fresh run must use a new host process", hostPid, freshPid)
    }

    /**
     * 等待慢 flow 首个 tick（host 冷启动时间不定）。
     *
     * @param ticks 已收集日志行（同步 list）。
     * @throws AssertionError 超过 [FIRST_TICK_TIMEOUT_MS] 未收到。
     */
    private fun waitForFirstTick(ticks: List<String>) {
        val deadline = System.currentTimeMillis() + FIRST_TICK_TIMEOUT_MS
        while (System.currentTimeMillis() < deadline) {
            if (ticks.any { it.startsWith("diag:slow:tick:") }) return
            Thread.sleep(POLL_MS)
        }
        throw AssertionError(
            "slow flow did not emit first tick within ${FIRST_TICK_TIMEOUT_MS}ms; " +
                "collectedTail=" + ticks.takeLast(5),
        )
    }

    /**
     * 等待 launcher 回写 host pid。
     *
     * @param launcher 当前 launcher。
     * @return host pid；≤[HOST_PID_TIMEOUT_MS] 内未回写返回 -1。
     */
    private fun waitForHostPid(launcher: AppProcessFlowLauncher): Int {
        val deadline = System.currentTimeMillis() + HOST_PID_TIMEOUT_MS
        while (System.currentTimeMillis() < deadline) {
            val pid = launcher.lastHostPid
            if (pid > 0) return pid
            Thread.sleep(POLL_MS)
        }
        return launcher.lastHostPid
    }

    /**
     * 测试清理：向同 uid host 进程发 `kill -9`。
     *
     * @param pid 目标 pid。
     */
    private fun killPid(pid: Int) {
        runCatching {
            Runtime.getRuntime()
                .exec(arrayOf("/system/bin/kill", "-9", pid.toString()))
                .waitFor()
        }
    }

    /**
     * 等待进程消失。
     *
     * @param pid 目标 pid。
     * @param timeoutMs 上限（ms）。
     * @return true=已消失。
     */
    private fun awaitGone(pid: Int, timeoutMs: Long): Boolean {
        val deadline = System.currentTimeMillis() + timeoutMs
        while (System.currentTimeMillis() < deadline) {
            if (!File("/proc/$pid").exists()) return true
            Thread.sleep(POLL_MS)
        }
        return !File("/proc/$pid").exists()
    }

    private companion object {
        /** 慢 flow 首 tick 等待上限（ms）。 */
        const val FIRST_TICK_TIMEOUT_MS: Long = 10_000L

        /** host pid 回写等待上限（ms）。 */
        const val HOST_PID_TIMEOUT_MS: Long = 5_000L

        /** host 死亡到 host-lost 返回的上限（ms；应 ≤2s，测试宽限 10s）。 */
        const val HOST_DEATH_TIMEOUT_MS: Long = 10_000L

        /** 旧 pid 消失上限（ms）。 */
        const val OLD_PID_GONE_MS: Long = 5_000L

        /** /proc 轮询间隔（ms）。 */
        const val POLL_MS: Long = 50L
    }
}
