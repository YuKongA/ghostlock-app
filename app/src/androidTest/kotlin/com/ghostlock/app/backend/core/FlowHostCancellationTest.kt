package com.ghostlock.app.backend.core

import android.content.pm.PackageManager
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.ghostlock.app.backend.diagnostics.DiagnosticOkFlow
import com.ghostlock.app.backend.diagnostics.DiagnosticSlowFlow
import com.ghostlock.app.backend.launcher.AppProcessFlowLauncher
import com.ghostlock.app.backend.launcher.ShizukuProcessFlowLauncher
import com.ghostlock.app.core.flow.FlowResult
import com.ghostlock.app.backend.launcher.BackendExecutionState
import java.io.File
import java.util.Collections
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.cancelAndJoin
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith
import rikka.shizuku.Shizuku

/**
 * G10 设备取消门禁（本批只编译；真机由 Lead 下批执行）：
 *
 * a) 慢 flow（DiagnosticSlowFlow）协程 1.5s 后 `cancelAndJoin` ⇒ job.isCancelled；
 * b) 随后 DiagnosticOkFlow 新 run ⇒ Finished.Ok，host pid 不同且旧 pid `/proc` ≤5s 消失；
 * c) Shizuku 可用时对 ShizukuProcessFlowLauncher 跑同一取消用例，否则 assumeTrue skip。
 *
 * @note 无 fixture ELF 依赖、不启动真实攻击；只使用诊断 fixture 与新进程通道。
 */
@RunWith(AndroidJUnit4::class)
class FlowHostCancellationTest {
    private val context get() = InstrumentationRegistry.getInstrumentation().targetContext

    /** @return DiagnosticSlowFlow 的登记 symbol。 */
    private fun slowSymbol(): String = DiagnosticSlowFlow::class.java.name + "#entry"

    /** @return DiagnosticOkFlow 的登记 symbol。 */
    private fun okSymbol(): String = DiagnosticOkFlow::class.java.name + "#entry"

    @Test
    fun appLauncherCancellationCancelsHostAndStartsFreshProcess() = runBlocking {
        val ticks = Collections.synchronizedList(mutableListOf<String>())
        val launcher = AppProcessFlowLauncher(context)
        val job = launch(Dispatchers.Default) {
            launcher.run(slowSymbol(), emptyMap(), null) { line -> ticks += line }
        }
        waitForFirstTick(ticks)
        delay(CANCEL_SETTLE_MS)
        job.cancelAndJoin()
        assertTrue("cancelled coroutine must finish as cancelled", job.isCancelled)
        assertTrue(
            "slow flow must emit ticks before cancellation",
            ticks.any { it.startsWith("diag:slow:tick:") },
        )
        val cancelledPid = launcher.lastHostPid
        assertTrue("slow run must report host pid", cancelledPid > 0)

        val fresh = AppProcessFlowLauncher(context)
        val state = fresh.run(okSymbol(), emptyMap(), null) { }
        assertTrue("fresh run must finish: $state", state is BackendExecutionState.Finished)
        val result = (state as BackendExecutionState.Finished).result
        assertTrue("fresh run must be Ok: $result", result is FlowResult.Ok)
        val freshPid = fresh.lastHostPid
        assertTrue("fresh run must report host pid", freshPid > 0)
        assertNotEquals("every run must use a new host process", cancelledPid, freshPid)
        assertTrue(
            "cancelled host process must disappear within ${OLD_PID_GONE_MS}ms",
            awaitProcessGone(cancelledPid, OLD_PID_GONE_MS),
        )
    }

    @Test
    fun shizukuLauncherCancellationCancelsHostAndStartsFreshProcess() = runBlocking {
        val shizukuProbe = runCatching {
            val binder = Shizuku.pingBinder()
            val permission = Shizuku.checkSelfPermission() == PackageManager.PERMISSION_GRANTED
            val uid = Shizuku.getUid()
            val domain = Shizuku.getSELinuxContext()
            "binder=$binder permission=$permission uid=$uid domain=$domain" to
                (binder && permission && uid == 2000 && domain?.contains("u:r:shell:s0") == true)
        }
        val (probeSummary, shizukuReady) = shizukuProbe.getOrElse { problem ->
            ("probeError=" + problem::class.java.simpleName) to false
        }
        assumeTrue(
            "Shizuku unavailable on this device: $probeSummary",
            shizukuReady,
        )

        val ticks = Collections.synchronizedList(mutableListOf<String>())
        val launcher = ShizukuProcessFlowLauncher(context)
        val job = launch(Dispatchers.Default) {
            launcher.run(slowSymbol(), emptyMap(), null) { line -> ticks += line }
        }
        waitForFirstTick(ticks)
        delay(CANCEL_SETTLE_MS)
        job.cancelAndJoin()
        assertTrue("cancelled coroutine must finish as cancelled", job.isCancelled)
        assertTrue(
            "slow flow must emit ticks before cancellation",
            ticks.any { it.startsWith("diag:slow:tick:") },
        )
        val cancelledPid = launcher.lastHostPid
        assertTrue("slow run must report shell pid", cancelledPid > 0)

        val fresh = ShizukuProcessFlowLauncher(context)
        val state = fresh.run(okSymbol(), emptyMap(), null) { }
        assertTrue("fresh run must finish: $state", state is BackendExecutionState.Finished)
        assertEquals(
            "fresh run must be Ok",
            true,
            (state as BackendExecutionState.Finished).result is FlowResult.Ok,
        )
        val freshPid = fresh.lastHostPid
        assertTrue("fresh run must report shell pid", freshPid > 0)
        assertNotEquals("every run must use a new shell process", cancelledPid, freshPid)
        assertTrue(
            "cancelled shell process must disappear within ${OLD_PID_GONE_MS}ms",
            awaitProcessGone(cancelledPid, OLD_PID_GONE_MS),
        )
    }

    /**
     * 等待慢 flow 的首个 tick（覆盖 `:flowhost` JVM 冷启动，固定 delay 不可靠）。
     *
     * @param ticks 已收集日志行（Binder 线程写入；调用方使用同步 list）。
     * @throws AssertionError 超过 [FIRST_TICK_TIMEOUT_MS] 仍未收到首 tick（明确设备侧原因）。
     */
    private fun waitForFirstTick(ticks: List<String>) {
        val deadline = System.currentTimeMillis() + FIRST_TICK_TIMEOUT_MS
        while (System.currentTimeMillis() < deadline) {
            if (ticks.any { it.startsWith("diag:slow:tick:") }) return
            Thread.sleep(POLL_MS)
        }
        throw AssertionError(
            "slow flow did not emit first tick within ${FIRST_TICK_TIMEOUT_MS}ms " +
                "(flowhost cold start or host unavailable); " +
                "collectedTail=" + ticks.takeLast(5),
        )
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
        /** 慢 flow 首个 tick 等待上限（ms；覆盖 host JVM 冷启动）。 */
        const val FIRST_TICK_TIMEOUT_MS: Long = 10_000L

        /** 首个 tick 后到取消的稳定窗口（ms）。 */
        const val CANCEL_SETTLE_MS: Long = 200L

        /** 旧 host pid 消失上限（ms）。 */
        const val OLD_PID_GONE_MS: Long = 5_000L

        /** /proc 轮询间隔（ms）。 */
        const val POLL_MS: Long = 50L
    }
}
