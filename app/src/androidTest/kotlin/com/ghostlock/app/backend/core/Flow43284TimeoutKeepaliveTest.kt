package com.ghostlock.app.backend.core

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.ghostlock.app.backend.diagnostics.DiagnosticOkFlow
import com.ghostlock.app.backend.launcher.AppProcessFlowLauncher
import com.ghostlock.app.core.flow.FlowResult
import com.ghostlock.app.backend.launcher.BackendExecutionState
import java.io.File
import java.util.Collections
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith

/**
 * G12 设备门禁（本批只编译；由 Lead 用 slow fixture APK 跑）：
 * 在**生产 43284 entry + 真实 AndroidIpSecRuntime** 上触发 `exec.timeout_ms=2000` 超时，验证：
 *
 * - entry 返回 Failed(`flow-entry:43284:timeout`)；
 * - evidence `ipsec=runtime-leaked`、`host_keepalive=true`、`host_run_dir_cleanup=skipped`；
 * - host 进程保活；A11：payload 子进程（`lib43284Glued.so`）**未被 kill**；
 * - 测试内回收 keepalive host/payload 后，新 run 使用新进程。
 *
 * only-slow-fixture：`lib43284Glued.so` 必须是 shell 脚本 marker
 * `fixture=ghostlock-exec-sleep-v1`（sleep 6）；其它环境一律 assumeTrue skip，不失败。
 *
 * @note 不跑真实攻击；43284 真实漏洞冷机门禁属 B5。
 */
@RunWith(AndroidJUnit4::class)
class Flow43284TimeoutKeepaliveTest {
    private val context get() = InstrumentationRegistry.getInstrumentation().targetContext

    /** 43284 生产 entry 的登记符号（字面量，避免引用入口类）。 */
    private val gluedSymbol: String =
        "com.ghostlock.app.backend.backends.cve43284glued.Flow43284GluedEntry#entry"

    /** slow fixture 的 marker（lib43284Glued.so 为 sleep 6 的 shell 脚本）。 */
    private val slowFixtureMarker: String = "fixture=ghostlock-exec-sleep-v1"

    /** @return true=当前 APK 安装了 slow fixture（否则 skip）。 */
    private fun slowFixtureInstalled(): Boolean = runCatching {
        val binary = File(context.applicationInfo.nativeLibraryDir, "lib43284Glued.so")
        binary.isFile && binary.readText().contains(slowFixtureMarker)
    }.getOrDefault(false)

    @Test
    fun timeoutLeaksRuntimeKeepsHostAliveAndDoesNotKillPayload() = runBlocking {
        assumeTrue(
            "slow 43284 fixture (fixture=ghostlock-exec-sleep-v1) is not installed",
            slowFixtureInstalled(),
        )
        val lines = Collections.synchronizedList(mutableListOf<String>())
        var hostPid = -1
        var payloadPid = -1
        var oldHostPid = -1
        try {
            val launcher = AppProcessFlowLauncher(context)
            val state = launcher.run(
                gluedSymbol,
                mapOf("exec.timeout_ms" to TIMEOUT_MS),
                null,
            ) { line -> lines += line }
            assertTrue("expected Finished, got $state", state is BackendExecutionState.Finished)
            val result = (state as BackendExecutionState.Finished).result
            assertTrue("expected Failed, got $result", result is FlowResult.Failed)
            val failed = result as FlowResult.Failed
            assertTrue(
                "timeout reason missing: ${failed.reason}",
                failed.reason.contains("flow-entry:43284:timeout"),
            )
            assertEquals(
                "ipsec evidence: ${failed.evidence}",
                "runtime-leaked",
                failed.evidence["ipsec"],
            )
            assertEquals(
                "keepalive evidence: ${failed.evidence}",
                "true",
                failed.evidence["host_keepalive"],
            )
            assertEquals(
                "run_dir cleanup must be skipped: ${failed.evidence}",
                "skipped",
                failed.evidence["host_run_dir_cleanup"],
            )
            hostPid = launcher.lastHostPid
            oldHostPid = hostPid
            assertTrue("host pid must be reported", hostPid > 0)
            assertNotEquals(
                "host must not be app process",
                hostPid,
                android.os.Process.myPid(),
            )
            assertTrue(
                "keepalive host must still be alive after run returns (pid=$hostPid)",
                File("/proc/$hostPid").exists(),
            )
            payloadPid = findPayloadPid(hostPid)
            assertTrue(
                "A11 violated: payload lib43284Glued.so must still be alive after timeout " +
                    "(hostPid=$hostPid, scanned=${payloadScanSummary()}, logs=${lines.takeLast(5)})",
                payloadPid > 0,
            )
        } finally {
            /* 测试清理（A11 生产语义有意保留）：先回收 keepalive host，再等/杀 payload。 */
            if (hostPid > 0) {
                killPid(hostPid)
                awaitGone(hostPid, HOST_GONE_MS)
            }
            if (payloadPid > 0) {
                if (!awaitGone(payloadPid, PAYLOAD_NATURAL_EXIT_MS)) {
                    killPid(payloadPid)
                    awaitGone(payloadPid, HOST_GONE_MS)
                }
            }
            /* 兜底：清理本测试可能残留的 43284 payload（避免设备残留）。 */
            for (pid in matchingPayloadPids()) {
                killPid(pid)
                awaitGone(pid, HOST_GONE_MS)
            }
        }
        assertTrue(
            "reclaimed host must be gone (pid=$oldHostPid)",
            !File("/proc/$oldHostPid").exists(),
        )

        val fresh = AppProcessFlowLauncher(context)
        val freshState = fresh.run(
            DiagnosticOkFlow::class.java.name + "#entry",
            emptyMap(),
            null,
        ) { }
        assertTrue(
            "fresh run must finish: $freshState",
            freshState is BackendExecutionState.Finished,
        )
        val freshResult = (freshState as BackendExecutionState.Finished).result
        assertTrue("fresh run must be Ok: $freshResult", freshResult is FlowResult.Ok)
        val freshPid = fresh.lastHostPid
        assertTrue("fresh run must report host pid", freshPid > 0)
        assertNotEquals(
            "fresh run must use a new host process",
            oldHostPid,
            freshPid,
        )
    }

    /**
     * 在 `/proc/<pid>/cmdline` 中寻找 43284 payload 子进程。
     *
     * @param hostPid host 进程 pid（排除）。
     * @return payload pid；≤[PAYLOAD_SCAN_MS] 内找不到返回 -1。
     */
    private fun findPayloadPid(hostPid: Int): Int {
        val deadline = System.currentTimeMillis() + PAYLOAD_SCAN_MS
        while (System.currentTimeMillis() < deadline) {
            matchingPayloadPids(hostPid).firstOrNull()?.let { return it }
            Thread.sleep(POLL_MS)
        }
        return -1
    }

    /** @return cmdline 含 `lib43284Glued.so` 的进程 pid（可排除 hostPid）。 */
    private fun matchingPayloadPids(excludePid: Int = -1): List<Int> =
        numericPids().filter { pid ->
            pid != excludePid && readCmdline(pid).contains("lib43284Glued.so")
        }

    /** @return 扫描摘要（失败消息用；≤5 条）。 */
    private fun payloadScanSummary(): String = numericPids()
        .mapNotNull { pid ->
            val cmdline = readCmdline(pid).replace('\u0000', ' ').trim()
            if (cmdline.contains("lib43284") || cmdline.contains("ghostlock")) {
                "$pid=$cmdline"
            } else {
                null
            }
        }
        .take(5)
        .toString()

    /** @return /proc 下的数字 pid 列表（同 uid 可读范围内）。 */
    private fun numericPids(): List<Int> = runCatching {
        File("/proc").listFiles()
            ?.mapNotNull { entry -> entry.name.toIntOrNull() }
            ?: emptyList()
    }.getOrDefault(emptyList())

    /** @return 进程 cmdline 文本（读失败空串）。 */
    private fun readCmdline(pid: Int): String =
        runCatching { File("/proc/$pid/cmdline").readText() }.getOrDefault("")

    /**
     * 测试清理：向同 uid 进程发 `kill -9`。
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
        /** 生产 entry 超时参数（ms；slow fixture sleep 6s ⇒ 必超时）。 */
        const val TIMEOUT_MS: Long = 2_000L

        /** payload 扫描窗口（ms）。 */
        const val PAYLOAD_SCAN_MS: Long = 2_000L

        /** host 回收等待（ms）。 */
        const val HOST_GONE_MS: Long = 5_000L

        /** payload 自然退出等待（ms；脚本 sleep 6）。 */
        const val PAYLOAD_NATURAL_EXIT_MS: Long = 15_000L

        /** /proc 轮询间隔（ms）。 */
        const val POLL_MS: Long = 50L
    }
}
