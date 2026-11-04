package com.ghostlock.app.backend.core

import com.ghostlock.app.backend.host.FlowHostProtocol
import com.ghostlock.app.core.flow.FlowResult
import com.ghostlock.app.backend.launcher.BackendExecutionState
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/** B2a：verify 行协议编解码/转义/映射/稳定码矩阵（纯 JVM）。 */
class FlowHostProtocolTest {
    @Test
    fun `stable codes and states`() {
        assertEquals(0, FlowHostProtocol.OK)
        assertEquals(1, FlowHostProtocol.ERR_ARGS)
        assertEquals(2, FlowHostProtocol.ERR_SYMBOL)
        assertEquals(3, FlowHostProtocol.ERR_BUSY)
        assertEquals(4, FlowHostProtocol.ERR_STATE)
        assertEquals(5, FlowHostProtocol.ERR_START)
        assertEquals(6, FlowHostProtocol.ERR_IO)
        assertEquals(0, FlowHostProtocol.IDLE)
        assertEquals(1, FlowHostProtocol.PREPARED)
        assertEquals(2, FlowHostProtocol.RUNNING)
        assertEquals(3, FlowHostProtocol.TERMINAL)
        assertEquals(0, FlowHostProtocol.TERMINAL_CODE_OK)
        assertEquals(1, FlowHostProtocol.TERMINAL_CODE_FAILED)
        assertEquals(2, FlowHostProtocol.TERMINAL_CODE_HANDOFF)
    }

    @Test
    fun `ok verify roundtrip with escaped evidence`() {
        val result = FlowResult.Ok(
            steps = emptyList(),
            evidence = mapOf("key" to "x=y\nz\\w"),
        )
        val decoded = FlowHostProtocol.decodeVerify(FlowHostProtocol.encodeVerify(result))
        assertEquals(FlowHostProtocol.TerminalResult.OK, decoded.result)
        assertEquals(0, decoded.steps)
        assertEquals("x=y\nz\\w", decoded.evidence["key"])
    }

    @Test
    fun `failed verify roundtrip keeps reason and evidence`() {
        val result = FlowResult.Failed(
            reason = "diag:failed\nphase2",
            failedStep = null,
            steps = emptyList(),
            evidence = mapOf("diag" to "reason"),
        )
        val decoded = FlowHostProtocol.decodeVerify(FlowHostProtocol.encodeVerify(result))
        assertEquals(FlowHostProtocol.TerminalResult.FAILED, decoded.result)
        assertEquals("diag:failed\nphase2", decoded.reason)
        assertEquals("reason", decoded.evidence["diag"])
    }

    @Test
    fun `handoff verify maps to HandoffRequested and never success`() {
        val bytes = FlowHostProtocol.encodeVerify(
            FlowResult.Ok(emptyList(), mapOf("handoffExtra" to "1")),
            handoff = true,
        )
        val decoded = FlowHostProtocol.decodeVerify(bytes)
        assertEquals(FlowHostProtocol.TerminalResult.HANDOFF, decoded.result)
        assertEquals(BackendExecutionState.HandoffRequested, FlowHostProtocol.mapTerminal(decoded))
    }

    @Test
    fun `handoff encoding rejects failed result`() {
        val failed = FlowResult.Failed("f", null, emptyList(), emptyMap())
        val thrown = runCatching { FlowHostProtocol.encodeVerify(failed, handoff = true) }
        assertTrue(thrown.exceptionOrNull() is IllegalArgumentException)
    }

    @Test
    fun `decode rejects malformed protocols`() {
        val cases = mapOf(
            "flow-host:verify:empty" to ByteArray(0),
            "flow-host:verify:missing-final-newline" to
                "state=terminal\nresult=ok\nsteps=0".toByteArray(),
            "flow-host:verify:bad-state" to
                "state=idle\nresult=ok\nsteps=0\n".toByteArray(),
            "flow-host:verify:unknown-key" to
                "state=terminal\nresult=ok\nsteps=0\nbogus=1\n".toByteArray(),
            "flow-host:verify:bad-steps" to
                "state=terminal\nresult=ok\nsteps=x\n".toByteArray(),
            "flow-host:verify:missing-steps" to
                "state=terminal\nresult=ok\n".toByteArray(),
            "flow-host:verify:bad-result" to
                "state=terminal\nresult=weird\nsteps=0\n".toByteArray(),
            "flow-host:verify:duplicate-key" to
                "state=terminal\nresult=ok\nresult=failed\nsteps=0\n".toByteArray(),
        )
        for ((expectedPrefix, bytes) in cases) {
            val problem = runCatching { FlowHostProtocol.decodeVerify(bytes) }.exceptionOrNull()
            assertTrue(
                "expected $expectedPrefix but got ${problem?.message}",
                problem is IllegalArgumentException &&
                    problem.message!!.startsWith(expectedPrefix),
            )
        }
    }

    @Test
    fun `failed verify without reason rejects`() {
        val problem = runCatching {
            FlowHostProtocol.decodeVerify(
                "state=terminal\nresult=failed\nsteps=0\n".toByteArray(),
            )
        }.exceptionOrNull()
        assertEquals("flow-host:verify:missing-reason", problem!!.message)
    }

    @Test
    fun `mapTerminal ok failed and identity`() {
        val okBytes = FlowHostProtocol.encodeVerify(
            FlowResult.Ok(emptyList(), mapOf("k" to "v")),
        )
        val okState = FlowHostProtocol.mapTerminal(
            FlowHostProtocol.decodeVerify(okBytes),
            profileIdentity = "profile-1",
        )
        assertTrue(okState is BackendExecutionState.Finished)
        val okResult = (okState as BackendExecutionState.Finished).result
        assertTrue(okResult is FlowResult.Ok)
        assertEquals("v", okResult.evidence["k"])
        assertEquals("profile-1", okResult.evidence["profileIdentity"])

        val failedBytes = FlowHostProtocol.encodeVerify(
            FlowResult.Failed("r", null, emptyList(), mapOf("e" to "1")),
        )
        val failedState = FlowHostProtocol.mapTerminal(FlowHostProtocol.decodeVerify(failedBytes))
        assertTrue(failedState is BackendExecutionState.Finished)
        val failedResult = (failedState as BackendExecutionState.Finished).result
        assertTrue(failedResult is FlowResult.Failed)
        val failed = failedResult as FlowResult.Failed
        assertEquals("r", failed.reason)
        assertEquals("1", failed.evidence["e"])
    }

    @Test
    fun `protocol bytes are utf8 and newline terminated`() {
        val bytes = FlowHostProtocol.encodeVerify(FlowResult.Ok(emptyList(), emptyMap()))
        assertTrue(bytes.isNotEmpty())
        assertEquals('\n'.code.toByte(), bytes.last())
        val text = bytes.toString(Charsets.UTF_8)
        assertEquals("state=terminal\nresult=ok\nsteps=0\n", text)
    }
}
