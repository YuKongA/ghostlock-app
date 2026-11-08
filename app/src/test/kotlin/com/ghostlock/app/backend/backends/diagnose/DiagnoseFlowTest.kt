package com.ghostlock.app.backend.backends.diagnose

import com.ghostlock.app.backend.backends.diagnose.jna.DiagnoseNativeBinding
import com.ghostlock.app.core.flow.BackendFlowInput
import com.ghostlock.app.core.flow.FlowReportContext
import com.ghostlock.app.core.flow.FlowTerminalClaim
import com.ghostlock.app.core.rootscript.PublishedArtifactIdentity
import com.ghostlock.app.core.rootscript.RootScriptArtifact
import com.ghostlock.app.core.run.KotlinLogConnector
import com.ghostlock.app.domain.run.DiagnosticOutcome
import com.ghostlock.app.domain.run.ExitKind
import com.ghostlock.app.domain.run.LogDraft
import com.ghostlock.app.domain.run.LogConnectorEnd
import com.ghostlock.app.domain.run.LogEmitter
import com.ghostlock.app.domain.run.LogLevel
import com.ghostlock.app.domain.run.LogOrigin
import com.ghostlock.app.domain.run.ProfileSnapshot
import com.ghostlock.app.domain.run.ResultReason
import com.ghostlock.app.domain.run.RunCancellationView
import com.ghostlock.app.domain.run.RunCapability
import com.ghostlock.app.domain.run.RunTerminal
import com.ghostlock.app.domain.run.StageState
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicInteger
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.async
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** Host state-machine cases for the one-step Diagnose native/handoff chain. */
class DiagnoseFlowTest {
    /** One native call produces independent successful primitive and dispatch facts. */
    @Test
    fun nativeChainCompletesBothFacts() = runBlocking {
        val records = mutableListOf<LogDraft>()
        val closed = AtomicBoolean(false)
        val source = KotlinLogConnector()
        val collection = async(start = CoroutineStart.UNDISPATCHED) { source.collect(records::add) }
        val report = flow(successBinding(closed)).run(input(logs = source))
        source.finishInput()

        assertEquals(LogConnectorEnd.Drained, collection.await())
        assertEquals(StageState.SUCCESS, report.backendPrimitive.state)
        assertEquals(StageState.SUCCESS, report.rootScriptDispatch.state)
        assertEquals(ExitKind.PROCESS_EXIT, report.rootScriptDispatch.exitKind)
        assertEquals(StageState.SUCCESS, report.backendCleanup.state)
        assertEquals(DiagnosticOutcome.PASS, report.diagnosticOutcome)
        assertEquals(RunTerminal.DIAGNOSTIC_COMPLETE, report.terminal)
        assertFalse(report.isProductSuccess)
        assertTrue(closed.get())
        assertEquals(1, records.count { it.origin == LogOrigin.Kotlin && it.message == "diagnose:start" })
        assertEquals(1, records.count { it.origin == LogOrigin.Native && it.message == NATIVE_BODY })
        assertEquals(1, records.count { it.origin == LogOrigin.Shell && it.message == SHELL_BODY })
    }

    /** Function status remains the call authority and makes the untrusted dispatch payload skipped. */
    @Test
    fun nativeStatusRejectsResultPayload() = runBlocking {
        val binding = DiagnoseNativeBinding(
            chain = { _, _ -> successfulChain(status = -77, handoffStatus = 0) },
        )
        val report = flow(binding).run(input())

        assertEquals(StageState.FAILED, report.backendPrimitive.state)
        assertEquals(ResultReason.REJECTED, report.backendPrimitive.reason)
        assertEquals(ExitKind.NATIVE_STATUS, report.backendPrimitive.exitKind)
        assertEquals(-77, report.backendPrimitive.exitValue)
        assertEquals(StageState.SKIPPED, report.rootScriptDispatch.state)
        assertEquals(DiagnosticOutcome.FAIL, report.diagnosticOutcome)
    }

    /** A malformed Native record lowers only the primitive fact. */
    @Test
    fun malformedNativeRecordPreservesDispatchFact() = runBlocking {
        val report = flow(
            DiagnoseNativeBinding(
                chain = { _, _ -> successfulChain(nativeLogValid = false) },
            ),
        ).run(input())

        assertEquals(StageState.UNKNOWN, report.backendPrimitive.state)
        assertEquals(ResultReason.MALFORMED_RESULT, report.backendPrimitive.reason)
        assertEquals(StageState.SUCCESS, report.rootScriptDispatch.state)
        assertEquals(DiagnosticOutcome.UNKNOWN, report.diagnosticOutcome)
    }

    /** A malformed Shell record lowers only the dispatch fact. */
    @Test
    fun malformedShellRecordPreservesPrimitiveFact() = runBlocking {
        val report = flow(
            DiagnoseNativeBinding(
                chain = { _, _ -> successfulChain(shellLogValid = false) },
            ),
        ).run(input())

        assertEquals(StageState.SUCCESS, report.backendPrimitive.state)
        assertEquals(StageState.UNKNOWN, report.rootScriptDispatch.state)
        assertEquals(ResultReason.MALFORMED_RESULT, report.rootScriptDispatch.reason)
        assertEquals(DiagnosticOutcome.UNKNOWN, report.diagnosticOutcome)
    }

    /** A common helper failure is a real dispatch failure while the native primitive remains successful. */
    @Test
    fun helperFailureDoesNotOverwritePrimitiveSuccess() = runBlocking {
        val report = flow(
            DiagnoseNativeBinding(
                chain = { _, _ ->
                    successfulChain(
                        handoffStatus = DiagnoseNativeBinding.HANDOFF_SCRIPT_MAGIC,
                        shellLogValid = false,
                    )
                },
            ),
        ).run(input())

        assertEquals(StageState.SUCCESS, report.backendPrimitive.state)
        assertEquals(StageState.FAILED, report.rootScriptDispatch.state)
        assertEquals(ResultReason.ARTIFACT_IDENTITY_MISMATCH, report.rootScriptDispatch.reason)
        assertEquals(ExitKind.NATIVE_STATUS, report.rootScriptDispatch.exitKind)
        assertEquals(DiagnosticOutcome.FAIL, report.diagnosticOutcome)
    }

    /** Cancellation before the sole physical step prevents the native call and cancels both owned facts. */
    @Test
    fun cancellationBeforeNativeChainCancelsBothFacts() = runBlocking {
        val calls = AtomicInteger(0)
        val closed = AtomicBoolean(false)
        val binding = DiagnoseNativeBinding(
            chain = { _, nonce ->
                calls.incrementAndGet()
                successfulChain(echoedNonce = nonce)
            },
            closeAction = { closed.set(true) },
        )
        val cancellation = AtomicBoolean(true)
        val report = flow(binding).run(input(cancellation = cancellation))

        assertEquals(0, calls.get())
        assertEquals(StageState.CANCELLED, report.backendPrimitive.state)
        assertEquals(StageState.CANCELLED, report.rootScriptDispatch.state)
        assertEquals(StageState.SUCCESS, report.backendCleanup.state)
        assertEquals(DiagnosticOutcome.UNKNOWN, report.diagnosticOutcome)
        assertEquals(RunTerminal.CANCELLED, report.terminal)
        assertTrue(closed.get())
    }

    /** Creates one Flow with deterministic process identity and nonce authorities. */
    private fun flow(binding: DiagnoseNativeBinding): DiagnoseFlow = DiagnoseFlow(
        binding = binding,
        currentUid = { UID },
        currentPid = { PID },
        nonceFactory = { NONCE },
    )

    /** @return One successful seam that injects the same two records as the real binding. */
    private fun successBinding(closed: AtomicBoolean): DiagnoseNativeBinding = DiagnoseNativeBinding(
        chain = { logs, nonce ->
            logs.emit(LogDraft(LogOrigin.Native, LogLevel.Info, NATIVE_BODY))
            logs.emit(LogDraft(LogOrigin.Shell, LogLevel.Info, SHELL_BODY))
            successfulChain(echoedNonce = nonce)
        },
        closeAction = { closed.set(true) },
    )

    /** @return One immutable native-chain result matching the fixture process identity. */
    private fun successfulChain(
        status: Int = DiagnoseNativeBinding.STATUS_OK,
        echoedNonce: Long = NONCE,
        handoffStatus: Int = 0,
        nativeLogValid: Boolean = true,
        shellLogValid: Boolean = true,
    ): DiagnoseNativeBinding.ChainResult = DiagnoseNativeBinding.ChainResult(
        status = status,
        uid = UID,
        pid = PID,
        echoedNonce = echoedNonce,
        handoffStatus = handoffStatus,
        nativeLogValid = nativeLogValid,
        shellLogValid = shellLogValid,
    )

    /** @return One complete diagnostic Flow input with a cancellation-backed terminal claim. */
    private fun input(
        logs: LogEmitter = LogEmitter { },
        cancellation: AtomicBoolean = AtomicBoolean(false),
    ): BackendFlowInput {
        val identity = PublishedArtifactIdentity(
            device = 1uL,
            inode = 1uL,
            byteCount = 0L,
            sha256 = "0".repeat(64),
            mode = 0u,
        )
        return BackendFlowInput(
            runId = "0".repeat(32),
            generation = 1L,
            rootScript = RootScriptArtifact(
                runId = "0".repeat(32),
                generation = 1L,
                scriptPath = "/run/root-script/root-script.sh",
                resultPath = "/run/root-script/result",
                scriptIdentity = identity,
                resultIdentity = identity,
                retainedArtifacts = emptyList(),
            ),
            profile = ProfileSnapshot("profile", ProfileSnapshot.CURRENT_SCHEMA_VERSION, "0".repeat(64)),
            logs = logs,
            cancellation = RunCancellationView { cancellation.get() },
            reportContext = FlowReportContext(RunCapability.DIAGNOSTIC_ONLY, null),
            terminalClaim = FlowTerminalClaim { cancellation.get() },
        )
    }

    private companion object {
        const val UID: Int = 2000
        const val PID: Int = 4242
        const val NONCE: Long = -1L
        const val NATIVE_BODY: String = "diagnose:native:roundtrip-ok"
        const val SHELL_BODY: String = "diagnose:shell:handoff-ok"
    }
}
