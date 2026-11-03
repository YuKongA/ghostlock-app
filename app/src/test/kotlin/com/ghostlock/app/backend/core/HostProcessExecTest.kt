package com.ghostlock.app.backend.core

import com.ghostlock.app.backend.backends.cve43284glued.Flow43284GluedEntry
import com.ghostlock.app.backend.host.FlowEntryParams
import com.ghostlock.app.backend.host.HostProcessExec
import com.ghostlock.app.core.flow.FlowHostEnv
import com.ghostlock.app.core.flow.FlowHostParams
import com.ghostlock.app.core.flow.FlowResult
import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** B4b：HostProcessExec 纯逻辑（超时解析/env+argv 构造/结局映射；不启动真实 ELF）。 */
class HostProcessExecTest {
    @Test
    fun `timeout defaults and overrides`() {
        assertEquals(HostProcessExec.DEFAULT_TIMEOUT_MS, HostProcessExec.resolveTimeoutMs(params()))
        assertEquals(
            1_234L,
            HostProcessExec.resolveTimeoutMs(
                params(HostProcessExec.PARAM_TIMEOUT_KEY to 1_234),
            ),
        )
        assertEquals(
            2_345L,
            HostProcessExec.resolveTimeoutMs(
                params(HostProcessExec.PARAM_TIMEOUT_KEY to 2_345L),
            ),
        )
        assertEquals(
            HostProcessExec.MAX_TIMEOUT_MS,
            HostProcessExec.resolveTimeoutMs(
                params(HostProcessExec.PARAM_TIMEOUT_KEY to Long.MAX_VALUE),
            ),
        )
        assertEquals(
            HostProcessExec.DEFAULT_TIMEOUT_MS,
            HostProcessExec.resolveTimeoutMs(params(HostProcessExec.PARAM_TIMEOUT_KEY to 0)),
        )
        assertEquals(
            HostProcessExec.DEFAULT_TIMEOUT_MS,
            HostProcessExec.resolveTimeoutMs(params(HostProcessExec.PARAM_TIMEOUT_KEY to -5)),
        )
    }

    @Test
    fun `shared exec constants stay A11 safe`() {
        assertEquals(600_000L, HostProcessExec.DEFAULT_TIMEOUT_MS)
        assertEquals(3_600_000L, HostProcessExec.MAX_TIMEOUT_MS)
        assertEquals(100L, HostProcessExec.WAIT_SLICE_MS)
        assertTrue(HostProcessExec.JOIN_TIMEOUT_MS in 1L..5_000L)
    }

    @Test
    fun `spec composes argv and copies env`() {
        val environment = linkedMapOf("GHOSTLOCK_HOME" to "/run")
        val spec = HostProcessExec.spec(
            binary = File("/lib/lib43284Glued.so"),
            arguments = listOf("--encap-port", "51234"),
            directory = File("/run"),
            environment = environment,
            stdin = null,
            timeoutMs = 5_000L,
        )
        assertEquals("/lib/lib43284Glued.so", spec.argv.first())
        assertEquals(listOf("--encap-port", "51234"), spec.argv.drop(1))
        assertEquals("/run", spec.directory.absolutePath)
        assertNull(spec.stdin)
        assertEquals(5_000L, spec.timeoutMs)
        environment["GHOSTLOCK_HOME"] = "/mutated"
        assertEquals("/run", spec.environment["GHOSTLOCK_HOME"])
    }

    @Test
    fun `spec rejects nul argv and non positive timeout`() {
        val nul = runCatching {
            HostProcessExec.spec(
                File("/bin/x"),
                listOf("a\u0000b"),
                File("/run"),
                emptyMap(),
                null,
                1_000L,
            )
        }.exceptionOrNull()
        assertTrue(nul is IllegalArgumentException)
        val timeout = runCatching {
            HostProcessExec.spec(File("/bin/x"), emptyList(), File("/run"), emptyMap(), null, 0L)
        }.exceptionOrNull()
        assertTrue(timeout is IllegalArgumentException)
    }

    @Test
    fun `classify maps failures and terminal states`() {
        assertEquals(
            HostProcessExec.Outcome.StartFailed("missing"),
            HostProcessExec.classify(null, false, false, startFailure = "missing"),
        )
        assertEquals(
            HostProcessExec.Outcome.StdinFailed("io"),
            HostProcessExec.classify(null, false, false, stdinFailure = "io"),
        )
        assertEquals(
            HostProcessExec.Outcome.Cancelled,
            HostProcessExec.classify(null, true, true, stdinFailure = "io"),
        )
        assertEquals(
            HostProcessExec.Outcome.TimedOut(1_000L),
            HostProcessExec.classify(null, true, false, timeoutMs = 1_000L),
        )
        assertEquals(
            HostProcessExec.Outcome.TimedOut(1_000L),
            HostProcessExec.classify(
                null,
                true,
                false,
                stdinFailure = "io",
                timeoutMs = 1_000L,
            ),
        )
        assertEquals(
            HostProcessExec.Outcome.Completed(0),
            HostProcessExec.classify(0, false, false),
        )
        assertEquals(
            HostProcessExec.Outcome.Completed(-1),
            HostProcessExec.classify(null, false, false),
        )
        assertEquals(
            HostProcessExec.Outcome.StartFailed("x"),
            HostProcessExec.classify(0, true, true, startFailure = "x"),
        )
    }

    @Test
    fun `43284 entry requires host context and never creates runtime`() {
        val env = object : FlowHostEnv {
            override val context: android.content.Context? = null
            override val params: FlowHostParams = FlowHostParams.of(
                linkedMapOf(
                    FlowEntryParams.KEY_HOST_RUN_DIR to "/nonexistent",
                    FlowEntryParams.KEY_HOST_NATIVE_LIB_DIR to "/nonexistent",
                    FlowEntryParams.KEY_HOST_LOG_PATH to "/nonexistent/run.log",
                ),
            )
            override val payload: ByteArray? = null
            override fun log(line: String) = Unit
            override fun isCancelled(): Boolean = false
        }
        val result = Flow43284GluedEntry.entry(env)
        assertTrue(result is FlowResult.Failed)
        assertEquals("flow-host:context-required", (result as FlowResult.Failed).reason)
    }

    /** @return FlowHostParams（超时键值）。 */
    private fun params(vararg entries: Pair<String, Any?>): FlowHostParams =
        FlowHostParams.of(linkedMapOf(*entries))
}
