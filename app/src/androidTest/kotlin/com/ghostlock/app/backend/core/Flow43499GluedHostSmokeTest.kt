package com.ghostlock.app.backend.core

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
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith

/**
 * B4a-b 设备门禁（fixture APK；本批只编译）：
 * 仅当 APK 内 `lib43499-Glued.so` 携带 `fixture=ghostlock-exec-noop-v1` 标记时执行，
 * 否则 skip（生产 ELF 不运行真实漏洞）。
 *
 * @note 断言：gate allowed、Finished.Ok、输出含 fixture 标记、host pid≠app pid、
 *   `evidence.host_run_dir` 非空、两次 run host pid 不同。
 */
@RunWith(AndroidJUnit4::class)
class Flow43499GluedHostSmokeTest {
    private val context get() = InstrumentationRegistry.getInstrumentation().targetContext

    /** @return fixture ELF 是否已安装（仅编译/编译期不执行）。 */
    private fun fixtureInstalled(): Boolean {
        val binary = File(context.applicationInfo.nativeLibraryDir, "lib43499-Glued.so")
        val bytes = binary.takeIf { it.isFile }?.readBytes() ?: ByteArray(0)
        val text = bytes.toString(Charsets.ISO_8859_1)
        return text.contains("fixture=ghostlock-exec-noop-v1")
    }

    @Test
    fun gluedFlowRunsThroughNewProcessChannel() = runBlocking {
        assumeTrue("no-op fixture ELF is not installed", fixtureInstalled())
        val spec = BackendCve43499Glued
        val store = BackendConfigStore.open(context, spec.cve, spec.configSchema)
        val builtin = runCatching {
            Glued43499ProfileCodec.builtins(context).firstOrNull()?.release
        }.getOrNull()
        assumeTrue("no builtin profile available", builtin != null)
        spec.selectProfile("builtin:$builtin", store)
        val decision = BackendRunGate.decide(spec, spec.readiness(), RunPageContext())
        assertTrue("gate rejected: ${decision.reasons}", decision.allowed)

        val firstOutput = mutableListOf<String>()
        val firstDispatch = BackendRunDispatcher.dispatch(spec, decision, context) { firstOutput += it }
        assertTrue(firstDispatch is BackendRunDispatch.Executed)
        val firstState = (firstDispatch as BackendRunDispatch.Executed).state
        assertTrue("not Finished: $firstState", firstState is BackendRunUiState.Finished)
        val firstResult = (firstState as BackendRunUiState.Finished).result
        assertTrue("not Ok: $firstResult", firstResult is FlowResult.Ok)
        assertTrue(firstOutput.any { it.contains("fixture=ghostlock-exec-noop-v1") })
        val firstPid = firstResult.evidence["host_pid"]
        assertTrue("host pid missing", firstPid != null && firstPid != "0")
        assertNotEquals("host must not be app process", firstPid, android.os.Process.myPid().toString())
        assertTrue("host_run_dir missing", !firstResult.evidence["host_run_dir"].isNullOrBlank())

        val secondOutput = mutableListOf<String>()
        val secondDispatch = BackendRunDispatcher.dispatch(spec, decision, context) { secondOutput += it }
        val secondState = (secondDispatch as BackendRunDispatch.Executed).state
        val secondResult = (secondState as BackendRunUiState.Finished).result
        assertTrue(secondResult is FlowResult.Ok)
        assertNotEquals(
            "each run must use a new host process",
            firstResult.evidence["host_pid"],
            secondResult.evidence["host_pid"],
        )
    }
}
