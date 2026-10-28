package com.ghostlock.app.core.victim

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * 公用 victim 脚本启动器单测（backend-agnostic；fake session；不触 JNI/真机）。
 *
 * @note 关键语义：execScript 超时/失败只回收 helper；结果探针失败仍 close；不 kill payload。
 */
class VictimScriptLauncherTest {
    private class FakeSession(
        private val startOk: Boolean = true,
        private val exec: VictimResult<VictimExecResult> = VictimResult.Ok(
            VictimExecResult(0, "sha", 0, false, false, 0),
        ),
    ) : VictimScriptLauncher.VictimScriptSession {
        var starts = 0
        var closes = 0
        var lastScriptPath: String? = null
        var lastTimeoutMs: Long = 0L

        override fun start(): VictimResult<Unit> {
            starts += 1
            return if (startOk) VictimResult.Ok(Unit) else VictimResult.failed("victim:spawn-failed")
        }

        override fun execScript(scriptPath: String, timeoutMs: Long): VictimResult<VictimExecResult> {
            lastScriptPath = scriptPath
            lastTimeoutMs = timeoutMs
            return exec
        }

        override fun close() {
            closes += 1
        }
    }

    private fun config(): VictimSessionConfig = VictimSessionConfig(
        helperPath = "/data/app/lib/libghostlock_victim_helper.so",
        helperArgv = listOf("libghostlock_victim_helper.so"),
        helperEnv = emptyList(),
        capabilities = VictimCapabilities.of(
            VictimCapability.SPAWN_HELPER,
            VictimCapability.EXEC_SCRIPT,
            VictimCapability.BYE,
        ),
        rawSizeBytes = 64L * 1024L,
    )

    private fun launcher(session: FakeSession, written: MutableList<ByteArray> = mutableListOf()): VictimScriptLauncher =
        VictimScriptLauncher(
            sessionFactory = { session },
            scriptWriter = { _, bytes -> written += bytes; true },
        )

    @Test
    fun launchRunsScriptThenBackendProbeAndClosesHelper() {
        val session = FakeSession()
        val result = launcher(session).launch(
            config = config(),
            scriptPath = "/data/local/tmp/launch.sh",
            script = "#!/system/bin/sh\nexit 0\n".toByteArray(),
            execTimeoutMs = 5000L,
            onScriptDone = { VictimResult.Ok(4242) },
        )
        assertEquals(4242, (result as VictimResult.Ok).value)
        assertEquals(1, session.starts)
        assertEquals(1, session.closes)
        assertEquals("/data/local/tmp/launch.sh", session.lastScriptPath)
        assertTrue(session.lastTimeoutMs in 1..10_000)
    }

    @Test
    fun execTimeoutClosesHelperAndNeverTouchesPayload() {
        val session = FakeSession(
            exec = VictimResult.Ok(VictimExecResult(0, "sha", 0, false, true, 0)),
        )
        val result = launcher(session).launch(
            config = config(),
            scriptPath = "/s",
            script = byteArrayOf(1),
            execTimeoutMs = 5000L,
            onScriptDone = { error("must not probe after timeout") },
        )
        assertEquals("victim-script:timeout", (result as VictimResult.Failed).reason)
        assertEquals(1, session.closes)
    }

    @Test
    fun scriptWriteFailureDoesNotStartSession() {
        val session = FakeSession()
        val launcher = VictimScriptLauncher(
            sessionFactory = { session },
            scriptWriter = { _, _ -> false },
        )
        val result = launcher.launch(
            config = config(),
            scriptPath = "/s",
            script = byteArrayOf(1),
            execTimeoutMs = 5000L,
            onScriptDone = { VictimResult.Ok(1) },
        )
        assertEquals("victim-script:write", (result as VictimResult.Failed).reason)
        assertEquals(0, session.starts)
    }

    @Test
    fun probeFailureStillClosesHelper() {
        val session = FakeSession()
        val result = launcher(session).launch(
            config = config(),
            scriptPath = "/s",
            script = byteArrayOf(1),
            execTimeoutMs = 5000L,
            onScriptDone = { VictimResult.failed("backend:pid-missing") },
        )
        assertEquals("backend:pid-missing", (result as VictimResult.Failed).reason)
        assertEquals(1, session.closes)
    }

    @Test
    fun startFailurePropagatesAndCloses() {
        val session = FakeSession(startOk = false)
        val result = launcher(session).launch(
            config = config(),
            scriptPath = "/s",
            script = byteArrayOf(1),
            execTimeoutMs = 5000L,
            onScriptDone = { VictimResult.Ok(1) },
        )
        assertEquals("victim:spawn-failed", (result as VictimResult.Failed).reason)
        assertEquals(1, session.closes)
    }

    @Test
    fun invalidArgumentsFailClosedBeforeScriptWrite() {
        val session = FakeSession()
        val result = launcher(session).launch(
            config = config(),
            scriptPath = "",
            script = ByteArray(0),
            execTimeoutMs = 0L,
            onScriptDone = { VictimResult.Ok(1) },
        )
        assertEquals("victim:invalid-argument", (result as VictimResult.Failed).reason)
        assertEquals(0, session.starts)
    }
}
