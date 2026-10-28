package com.ghostlock.app.backend.cve98260.flow

import com.ghostlock.app.backend.cve98260.profile.Cve98260Profile
import com.ghostlock.app.backend.cve98260.run.Cve98260SafetyPort
import com.ghostlock.app.backend.cve98260.run.Cve98260ServicePort
import com.ghostlock.app.core.flow.FlowProgress
import com.ghostlock.app.core.flow.FlowResult
import com.ghostlock.app.data.cve98260.Cve98260ProfileConfig
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test

/**
 * B2 seam 测试：98260 app Flow 与 fake port/safety 的完整编排语义。
 *
 * 覆盖（delta §K）：validate 失败矩阵、prepare/start 未 armed 失败、park/超时/verify 失败的
 * mustReboot、成功也 must_reboot=true、取消传播 + cleanup 恰好一次且不 unbind(remove)、one-shot。
 *
 * 纯 JVM：不触 Android/AIDL/JNI/真机；生产实现由 Cve98260ServiceClient/RemoteExecutor 提供。
 */
class Flow98260ImplTest {

    // ---------- fakes ----------

    private class FakePort(
        var prepareResult: Int = Cve98260ServicePort.Code.OK,
        var prepareReason: String = "",
        var startResult: Int = 4242,
        var verifyBytes: ByteArray = (
            "ksu_loaded=1\nmodules_readable=1\npayload_pid=4242\n" +
                "finish_script_present=1\n" +
                "finish_script_sha256=e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855\n" +
                "finish_exit=0\nfinish_timeout=0\nfinish_uid=0\nfinish_enforce=1\n" +
                "finish_ksud=1\nfinish_ok=run-test\nfinish_fail_present=0"
            ).toByteArray(),
        var logChunks: MutableList<ByteArray> = mutableListOf(),
        var lastReasonText: String = "",
    ) : Cve98260ServicePort {
        val calls = mutableListOf<String>()
        val disconnects = mutableListOf<Boolean>()

        override suspend fun prepare(
            profileHex: String,
            payload: ByteArray,
            finishScript: ByteArray,
            runId: String,
            helperPath: String,
            onLine: (String) -> Unit,
        ): Int {
            calls += "prepare:" + profileHex.substringBefore('|').length
            return prepareResult
        }

        override suspend fun start(): Int {
            calls += "start"
            return startResult
        }

        override suspend fun state(): Int = Cve98260ServicePort.State.RUNNING

        override suspend fun readLog(offset: Long, maxBytes: Int): ByteArray {
            calls += "readLog:" + offset
            return if (logChunks.isEmpty()) ByteArray(0) else logChunks.removeAt(0)
        }

        override suspend fun verify(): ByteArray {
            calls += "verify"
            return verifyBytes
        }

        override suspend fun lastReason(): String = prepareReason.ifEmpty { lastReasonText }

        override suspend fun bootId(): String = "boot-1"

        override suspend fun disconnect(remove: Boolean) {
            calls += "disconnect:" + remove
            disconnects += remove
        }
    }

    private class FakeSafety(
        var release: String? = TARGET_RELEASE,
        var boot: String? = "boot-1",
        var blocked: Boolean = false,
        var launchPendingOk: Boolean = true,
        var terminalOk: Boolean = true,
    ) : Cve98260SafetyPort {
        val terminals = mutableListOf<Triple<Boolean, String, String?>>()

        override fun kernelRelease(): String? = release
        override fun bootId(): String? = boot
        override fun storeBlocked(): Boolean = blocked
        override fun markLaunchPending(bootId: String?): Boolean = launchPendingOk
        override fun markTerminal(
            bootId: String?,
            mustReboot: Boolean,
            outcome: String,
            reason: String?,
        ): Boolean {
            terminals += Triple(mustReboot, outcome, reason)
            return terminalOk
        }
    }

    private fun profile(): Cve98260Profile = Cve98260ProfileConfig.parse(VALID_PROFILE)

    private fun request(
        port: FakePort,
        safety: FakeSafety,
        progress: FlowProgress = FlowProgress.NONE,
        pollWindowMs: Long = 200L,
        pollIntervalMs: Long = 1L,
    ): Flow98260Request = Flow98260Request(
        runId = "run-test",
        profile = profile(),
        payload = byteArrayOf(1, 2, 3),
        port = port,
        safety = safety,
        progress = progress,
        pollIntervalMs = pollIntervalMs,
        pollWindowMs = pollWindowMs,
    )

    // ---------- tests ----------

    @Test
    fun validateRejectsWhenEnvironmentNotReady() = runBlocking {
        data class Case(
            val safety: FakeSafety,
            val expected: String,
        )
        val cases = listOf(
            Case(FakeSafety(release = "6.12.70-other"), "cve98260:validate:release-mismatch:"),
            Case(FakeSafety(boot = null), "cve98260:validate:boot-id-unknown"),
            Case(FakeSafety(blocked = true), "cve98260:validate:must-reboot"),
        )
        for (case in cases) {
            val port = FakePort()
            val result = Flow98260Impl().run(request(port, case.safety))
            assertTrue(result is FlowResult.Failed)
            assertTrue(
                "expected prefix " + case.expected + " got " + (result as FlowResult.Failed).reason,
                result.reason.startsWith(case.expected),
            )
            assertFalse(
                "validate must not call service",
                port.calls.any { it.startsWith("prepare") || it == "start" },
            )
        }
    }

    @Test
    fun prepareRefusalIsNotArmedAndDoesNotStart() = runBlocking {
        val port = FakePort(
            prepareResult = Cve98260ServicePort.Code.ERR_PREFLIGHT,
            prepareReason = "cve98260:preflight:slab-order:5",
        )
        val safety = FakeSafety()
        val result = Flow98260Impl().run(request(port, safety))
        assertTrue(result is FlowResult.Failed)
        assertTrue((result as FlowResult.Failed).reason.contains("cve98260:preflight:slab-order:5"))
        assertFalse(port.calls.any { it == "start" })
        assertTrue(safety.terminals.isNotEmpty())
        assertFalse("未 armed 不应 must_reboot", safety.terminals.last().first)
    }

    @Test
    fun launchPendingPersistFailureDoesNotCallPrepare() = runBlocking {
        val port = FakePort()
        val safety = FakeSafety(launchPendingOk = false)
        val result = Flow98260Impl().run(request(port, safety))
        assertTrue(result is FlowResult.Failed)
        assertEquals("cve98260:prepare:persist-failed", (result as FlowResult.Failed).reason)
        assertFalse(port.calls.any { it.startsWith("prepare") || it == "start" })
    }

    @Test
    fun startFailureIsNamedAndNotArmed() = runBlocking {
        val port = FakePort(startResult = -Cve98260ServicePort.Code.ERR_SPAWN, lastReasonText = "spawn-2")
        val safety = FakeSafety()
        val result = Flow98260Impl().run(request(port, safety))
        assertTrue(result is FlowResult.Failed)
        assertTrue((result as FlowResult.Failed).reason.startsWith("cve98260:start:-6"))
        assertFalse(safety.terminals.last().first)
    }

    @Test
    fun parkDuringPollForcesMustReboot() = runBlocking {
        val port = FakePort(
            logChunks = mutableListOf(
                ("GLK98260_ENV pid=4242 uid=2000 domain=u:r:shell:s0 release=6.12.69-x\n" +
                    "GLK98260_PREFLIGHT=ok\nGLK98260_ARMED=1\nGLK98260_PARKED=fake cred page held\n")
                    .toByteArray(),
            ),
        )
        val safety = FakeSafety()
        val result = Flow98260Impl().run(request(port, safety))
        assertTrue(result is FlowResult.Failed)
        assertEquals("cve98260:park-unreaped", (result as FlowResult.Failed).reason)
        assertTrue("park 后必须落 must_reboot", safety.terminals.last().first)
        assertEquals("indeterminate", safety.terminals.last().second)
    }

    @Test
    fun pollWindowTimeoutIsIndeterminate() = runBlocking {
        val port = FakePort()
        val safety = FakeSafety()
        val result = Flow98260Impl().run(request(port, safety, pollWindowMs = 30L, pollIntervalMs = 2L))
        assertTrue(result is FlowResult.Failed)
        assertEquals("cve98260:poll-window-timeout", (result as FlowResult.Failed).reason)
        assertTrue(safety.terminals.last().first)
    }

    @Test
    fun verifyFailureIsIndeterminate() = runBlocking {
        val port = FakePort(
            logChunks = mutableListOf("GLK98260_ARMED=1\nKSU_LOADED\n".toByteArray()),
            verifyBytes = "ksu_loaded=0\nmodules_readable=1".toByteArray(),
        )
        val result = Flow98260Impl().run(request(port, FakeSafety()))
        assertTrue(result is FlowResult.Failed)
        assertEquals("cve98260:verify:ksu-not-loaded", (result as FlowResult.Failed).reason)
    }

    @Test
    fun successPersistsMustRebootBeforeOkAndUnbindsWithRemove() = runBlocking {
        val port = FakePort(
            logChunks = mutableListOf("GLK98260_ARMED=1\nKSU_LOADED\n".toByteArray()),
            verifyBytes = (
                "ksu_loaded=1\nmodules_readable=1\npayload_pid=4242\n" +
                    "finish_script_present=1\n" +
                    "finish_script_sha256=e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855\n" +
                    "finish_exit=0\nfinish_timeout=0\nfinish_uid=0\nfinish_enforce=1\n" +
                    "finish_ksud=1\nfinish_ok=run-test\nfinish_fail_present=0"
                ).toByteArray(),
        )
        val safety = FakeSafety()
        val progressLines = mutableListOf<String>()
        val result = Flow98260Impl().run(request(port, safety, FlowProgress { progressLines += it }))
        assertTrue("flow result=" + result, result is FlowResult.Ok)
        val ok = result as FlowResult.Ok
        assertEquals("true", ok.evidence["mustReboot"])
        assertTrue(safety.terminals.last().first)
        assertEquals("ok", safety.terminals.last().second)
        assertEquals(listOf(true), port.disconnects)
        assertTrue(progressLines.any { it.contains("KSU_LOADED") })
        /* 成功也不能只靠日志：verify 必须被调用。 */
        assertTrue(port.calls.contains("verify"))
    }

    @Test
    fun cancellationPropagatesAndCleanupUnbindsWithoutRemove() = runBlocking {
        val port = FakePort()
        val safety = FakeSafety()
        var cancelled: Throwable? = null
        val job = launch(start = CoroutineStart.UNDISPATCHED) {
            try {
                Flow98260Impl().run(request(port, safety, pollWindowMs = 10_000L, pollIntervalMs = 5L))
            } catch (problem: CancellationException) {
                cancelled = problem
                throw problem
            }
        }
        delay(50)
        job.cancel()
        job.join()
        assertNotNull(cancelled)
        assertTrue(cancelled is CancellationException)
        assertEquals(listOf(false), port.disconnects)
        assertTrue("取消不应伪造终态", safety.terminals.isEmpty())
    }

    @Test
    fun flowIsOneShot() = runBlocking {
        val port = FakePort(
            logChunks = mutableListOf("KSU_LOADED\n".toByteArray()),
        )
        val request = request(port, FakeSafety())
        val flow = Flow98260Impl()
        assertTrue(flow.run(request) is FlowResult.Ok)
        try {
            flow.run(request)
            fail("second run must be rejected")
        } catch (problem: IllegalStateException) {
            assertEquals("flow is one-shot", problem.message)
        }
    }

    private companion object {
        private const val TARGET_RELEASE =
            "6.12.69-android16-6-g242e1a07f878-ab15865446-4k"

        private val VALID_PROFILE = """
            release = "${TARGET_RELEASE}"
            kernel_major = 6
            page_size = 4096
            cpu = 2
            slab_order = 1
            sockbuf = 8192
            slide_max = "0x40000000"
            base_min = "0xffffff8000000000"
            base_max = "0xffffffc000000000"
            static {
              schema_version = 1
              timer_site = "0xffffffc0803718f4"
              timer_site_low21 = "0x1718f4"
              copy_process_low21 = "0xe94a4"
            }
            sizes { timer_alloc = 0x100, cred_req = 0xb8, cred_alloc = 0xc0, task_alloc_bytes = 0x1440 }
            offsets { cred_uid = 0x8, task_cred = 0x900, selinux_blob = 0x8 }
            symbols {
              selinux_state = "0xffffffc082888890"
              panic_on_warn = "0xffffffc0825dade0"
              root_user = "0xffffffc082602170"
              init_user_ns = "0xffffffc082601ef8"
              init_ucounts = "0xffffffc082603b08"
              init_groups = "0xffffffc082603638"
            }
            caps { fullcap = "0x1ffffffffff", selinux_sid_kernel = 1 }
            ksu {
              manager_package = "me.weishu.kernelsu"
              kmi = "android16-6.12"
              late_load_args = ["late-load", "--kmi", "android16-6.12", "--allow-shell",
                                "--package-name", "me.weishu.kernelsu"]
              ksud_fallbacks = ["/data/local/tmp/ksud"]
            }
        """.trimIndent()
    }
}
