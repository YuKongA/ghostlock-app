package com.ghostlock.plugin.diagnose

import com.ghostlock.plugin.api.v1.DiagnosticRunContext
import com.ghostlock.plugin.api.v1.PluginException
import com.ghostlock.plugin.api.v1.PluginExitKind
import com.ghostlock.plugin.api.v1.PluginFailureCode
import com.ghostlock.plugin.api.v1.PluginLogLevel
import com.ghostlock.plugin.api.v1.PluginLogOrigin
import com.ghostlock.plugin.api.v1.PluginResultReason
import com.ghostlock.plugin.api.v1.PluginStageState
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicInteger
import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test

class DiagnoseRunTest {
    @Test
    fun successfulNativeChainProducesIndependentSuccessfulFactsAndLogs() {
        val closed = AtomicBoolean(false)
        val context = TestContext()
        val run = run(
            binding = DiagnoseNativeBinding(
                chain = { callContext, nonce ->
                    callContext.emit(PluginLogOrigin.NATIVE, PluginLogLevel.INFO, NATIVE_BODY)
                    callContext.emit(PluginLogOrigin.SHELL, PluginLogLevel.INFO, SHELL_BODY)
                    successfulChain(echoedNonce = nonce)
                },
                closeAction = { closed.set(true) },
            ),
        )

        val result = run.execute(context)
        run.close()

        assertEquals(PluginStageState.SUCCESS, result.backendPrimitive().state())
        assertEquals(PluginStageState.SUCCESS, result.rootScriptDispatch().state())
        assertEquals(PluginExitKind.PROCESS_EXIT, result.rootScriptDispatch().exitKind())
        assertEquals(
            listOf(
                Triple(PluginLogOrigin.KOTLIN, PluginLogLevel.INFO, "diagnose:start"),
                Triple(PluginLogOrigin.NATIVE, PluginLogLevel.INFO, NATIVE_BODY),
                Triple(PluginLogOrigin.SHELL, PluginLogLevel.INFO, SHELL_BODY),
            ),
            context.logs,
        )
        assertTrue(closed.get())
    }

    @Test
    fun callStatusRejectsUntrustedPayloadAndSkipsDispatch() {
        val result = run(
            DiagnoseNativeBinding(chain = { _, _ -> successfulChain(status = -77, handoffStatus = 0) }),
        ).execute(TestContext())

        assertEquals(PluginStageState.FAILED, result.backendPrimitive().state())
        assertEquals(PluginResultReason.REJECTED, result.backendPrimitive().reason())
        assertEquals(PluginExitKind.NATIVE_STATUS, result.backendPrimitive().exitKind())
        assertEquals(-77, result.backendPrimitive().exitValue())
        assertEquals(PluginStageState.SKIPPED, result.rootScriptDispatch().state())
    }

    @Test
    fun malformedPhaseLogsLowerOnlyTheirOwnFacts() {
        val malformedNative = run(
            DiagnoseNativeBinding(chain = { _, _ -> successfulChain(nativeLogValid = false) }),
        ).execute(TestContext())
        assertEquals(PluginStageState.UNKNOWN, malformedNative.backendPrimitive().state())
        assertEquals(PluginResultReason.MALFORMED_RESULT, malformedNative.backendPrimitive().reason())
        assertEquals(PluginStageState.SUCCESS, malformedNative.rootScriptDispatch().state())

        val malformedShell = run(
            DiagnoseNativeBinding(chain = { _, _ -> successfulChain(shellLogValid = false) }),
        ).execute(TestContext())
        assertEquals(PluginStageState.SUCCESS, malformedShell.backendPrimitive().state())
        assertEquals(PluginStageState.UNKNOWN, malformedShell.rootScriptDispatch().state())
        assertEquals(PluginResultReason.MALFORMED_RESULT, malformedShell.rootScriptDispatch().reason())
    }

    @Test
    fun helperFailureDoesNotOverwritePrimitiveSuccess() {
        val result = run(
            DiagnoseNativeBinding(chain = { _, _ ->
                successfulChain(
                    handoffStatus = DiagnoseNativeBinding.HANDOFF_SCRIPT_MAGIC,
                    shellLogValid = false,
                )
            }),
        ).execute(TestContext())

        assertEquals(PluginStageState.SUCCESS, result.backendPrimitive().state())
        assertEquals(PluginStageState.FAILED, result.rootScriptDispatch().state())
        assertEquals(
            PluginResultReason.ARTIFACT_IDENTITY_MISMATCH,
            result.rootScriptDispatch().reason(),
        )
    }

    @Test
    fun cancellationBeforeCallCancelsBothFactsAndCloseStillOwnsBinding() {
        val calls = AtomicInteger(0)
        val closed = AtomicBoolean(false)
        val run = run(
            DiagnoseNativeBinding(
                chain = { _, nonce ->
                    calls.incrementAndGet()
                    successfulChain(echoedNonce = nonce)
                },
                closeAction = { closed.set(true) },
            ),
        )

        val result = run.execute(TestContext(cancelled = true))
        run.close()

        assertEquals(0, calls.get())
        assertEquals(PluginStageState.CANCELLED, result.backendPrimitive().state())
        assertEquals(PluginStageState.CANCELLED, result.rootScriptDispatch().state())
        assertTrue(closed.get())
    }

    @Test
    fun processIdentityMismatchIsInternalUnknownAndRunIsOneShot() {
        val run = run(
            DiagnoseNativeBinding(chain = { _, _ -> successfulChain(uid = UID + 1) }),
        )
        val result = run.execute(TestContext())

        assertEquals(PluginStageState.UNKNOWN, result.backendPrimitive().state())
        assertEquals(PluginResultReason.INTERNAL_CONTRACT_FAILURE, result.backendPrimitive().reason())
        assertEquals(PluginStageState.SKIPPED, result.rootScriptDispatch().state())
        assertThrows(IllegalStateException::class.java) { run.execute(TestContext()) }
    }

    @Test
    fun bindingFailureMapsToIoAndCloseFailureUsesCheckedPluginException() {
        val callFailure = run(
            DiagnoseNativeBinding(chain = { _, _ -> error("native failure") }),
        ).execute(TestContext())
        assertEquals(PluginStageState.FAILED, callFailure.backendPrimitive().state())
        assertEquals(PluginResultReason.IO_FAILURE, callFailure.backendPrimitive().reason())
        assertEquals(PluginStageState.SKIPPED, callFailure.rootScriptDispatch().state())

        val closeFailure = run(
            DiagnoseNativeBinding(
                chain = { _, _ -> successfulChain() },
                closeAction = { error("close failure") },
            ),
        )
        val problem = assertThrows(PluginException::class.java) { closeFailure.close() }
        assertEquals(PluginFailureCode.IO_FAILURE, problem.code())
    }

    private fun run(binding: DiagnoseNativeBinding): DiagnoseRun = DiagnoseRun(
        binding = binding,
        nonceFactory = { NONCE },
    )

    private fun successfulChain(
        status: Int = DiagnoseNativeBinding.STATUS_OK,
        uid: Int = UID,
        pid: Int = PID,
        echoedNonce: Long = NONCE,
        handoffStatus: Int = 0,
        nativeLogValid: Boolean = true,
        shellLogValid: Boolean = true,
    ): DiagnoseNativeBinding.ChainResult = DiagnoseNativeBinding.ChainResult(
        status = status,
        uid = uid,
        pid = pid,
        echoedNonce = echoedNonce,
        handoffStatus = handoffStatus,
        nativeLogValid = nativeLogValid,
        shellLogValid = shellLogValid,
    )

    private class TestContext(
        private val cancelled: Boolean = false,
    ) : DiagnosticRunContext {
        val logs = mutableListOf<Triple<PluginLogOrigin, PluginLogLevel, String>>()

        override fun runId(): String = "0".repeat(32)
        override fun generation(): Long = 1
        override fun processUid(): Int = UID
        override fun processPid(): Int = PID
        override fun cancellationRequested(): Boolean = cancelled
        override fun emit(origin: PluginLogOrigin, level: PluginLogLevel, message: String) {
            logs += Triple(origin, level, message)
        }
    }

    private companion object {
        const val UID = 2000
        const val PID = 4242
        const val NONCE = -1L
        const val NATIVE_BODY = "diagnose:native:roundtrip-ok"
        const val SHELL_BODY = "diagnose:shell:handoff-ok"
    }
}
