package com.ghostlock.app.backend.core

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.ghostlock.app.backend.diagnostics.DiagnosticHandoffFlow
import com.ghostlock.app.backend.diagnostics.DiagnosticOkFlow
import com.ghostlock.app.backend.host.FlowHostProtocol
import com.ghostlock.app.backend.launcher.AppProcessFlowLauncher
import com.ghostlock.app.core.flow.FlowResult
import com.ghostlock.app.ui.backend.BackendRunUiState
import java.io.File
import java.util.Collections
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/**
 * B2a/B2b：`:flowhost` 进程级 smoke（本批只编译；设备执行由 Lead B2b 门禁）。
 *
 * 覆盖：DiagnosticOkFlow 3 行日志 + Finished.Ok、连续两次 run 新 pid/旧进程消失、
 * 非法 symbol 拒绝、payload 超限拒绝、DiagnosticHandoffFlow → HandoffRequested。
 *
 * @note 只使用公开 API；不启动任何真实攻击/文件写。
 */
@RunWith(AndroidJUnit4::class)
class FlowHostProcessSmokeTest {
    private val context get() = InstrumentationRegistry.getInstrumentation().targetContext

    /** @return DiagnosticOkFlow 的登记 symbol。 */
    private fun okSymbol(): String = DiagnosticOkFlow::class.java.name + "#entry"

    /** @return DiagnosticHandoffFlow 的登记 symbol。 */
    private fun handoffSymbol(): String = DiagnosticHandoffFlow::class.java.name + "#entry"

    @Test
    fun diagnosticOkRunsInFreshHostProcess() = runBlocking {
        val lines = Collections.synchronizedList(mutableListOf<String>())
        val first = AppProcessFlowLauncher(context)
        val state = first.run(okSymbol(), mapOf("k" to "v"), null) { line -> lines += line }
        assertTrue("expected Finished, got $state", state is BackendRunUiState.Finished)
        assertTrue((state as BackendRunUiState.Finished).result is FlowResult.Ok)
        assertEquals(3, lines.count { it.startsWith("diag:line:") })
        val firstPid = first.lastHostPid
        assertTrue("host pid must be reported", firstPid > 0)
        val deadline = System.currentTimeMillis() + 5_000L
        while (System.currentTimeMillis() < deadline && File("/proc/$firstPid").exists()) {
            Thread.sleep(50L)
        }
        val second = AppProcessFlowLauncher(context)
        second.run(okSymbol(), emptyMap(), null) { }
        val secondPid = second.lastHostPid
        assertNotEquals("every run must use a new host process", firstPid, secondPid)
        assertFalse("old host process must disappear", File("/proc/$firstPid").exists())
    }

    @Test
    fun invalidSymbolIsRefused() = runBlocking {
        val state = AppProcessFlowLauncher(context)
            .run("com.ghostlock.app.core.flow.NoSuchFlow#entry", emptyMap(), null) { }
        assertTrue(state is BackendRunUiState.Refused)
        val reasons = (state as BackendRunUiState.Refused).reasons
        assertTrue(reasons.any { it.contains("symbol", ignoreCase = true) })
    }

    @Test
    fun oversizePayloadIsRefused() = runBlocking {
        val payload = ByteArray(FlowHostProtocol.MAX_PAYLOAD_BYTES + 1)
        val state = AppProcessFlowLauncher(context)
            .run(okSymbol(), emptyMap(), payload) { }
        assertTrue(state is BackendRunUiState.Refused)
        val reasons = (state as BackendRunUiState.Refused).reasons
        assertTrue(reasons.any { it.contains("payload", ignoreCase = true) })
    }

    @Test
    fun handoffIsNotReportedAsSuccess() = runBlocking {
        val state = AppProcessFlowLauncher(context)
            .run(handoffSymbol(), emptyMap(), null) { }
        assertEquals(BackendRunUiState.HandoffRequested, state)
    }
}
