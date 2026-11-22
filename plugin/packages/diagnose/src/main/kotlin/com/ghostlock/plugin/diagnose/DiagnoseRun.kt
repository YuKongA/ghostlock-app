package com.ghostlock.plugin.diagnose

import com.ghostlock.plugin.api.v1.DiagnosticBackendRun
import com.ghostlock.plugin.api.v1.DiagnosticRunContext
import com.ghostlock.plugin.api.v1.DiagnosticRunResult
import com.ghostlock.plugin.api.v1.PluginException
import com.ghostlock.plugin.api.v1.PluginExitKind
import com.ghostlock.plugin.api.v1.PluginFailureCode
import com.ghostlock.plugin.api.v1.PluginLogLevel
import com.ghostlock.plugin.api.v1.PluginLogOrigin
import com.ghostlock.plugin.api.v1.PluginResultReason
import com.ghostlock.plugin.api.v1.PluginStageResult
import com.ghostlock.plugin.api.v1.PluginStageState
import java.util.concurrent.atomic.AtomicBoolean

/** One-shot plugin implementation of the existing Diagnose native/handoff semantics. */
internal class DiagnoseRun internal constructor(
    private val binding: DiagnoseNativeBinding = DiagnoseNativeBinding(),
    private val nonceFactory: () -> Long = System::nanoTime,
) : DiagnosticBackendRun {
    private val executed = AtomicBoolean(false)
    private val closed = AtomicBoolean(false)

    override fun execute(context: DiagnosticRunContext): DiagnosticRunResult {
        check(!closed.get()) { "Diagnose run is closed" }
        check(executed.compareAndSet(false, true)) { "Diagnose run is one-shot" }
        if (context.cancellationRequested()) {
            return DiagnosticRunResult(CANCELLED, CANCELLED)
        }
        emit(context, PluginLogOrigin.KOTLIN, PluginLogLevel.INFO, "diagnose:start")
        val nonce = nonceFactory()
        val chain = try {
            binding.runChain(context, nonce)
        } catch (_: Throwable) {
            return DiagnosticRunResult(
                PluginStageResult(
                    PluginStageState.FAILED,
                    PluginResultReason.IO_FAILURE,
                    PluginExitKind.NONE,
                    0,
                ),
                SKIPPED,
            )
        }
        if (chain.status != DiagnoseNativeBinding.STATUS_OK) {
            return DiagnosticRunResult(
                PluginStageResult(
                    PluginStageState.FAILED,
                    PluginResultReason.REJECTED,
                    PluginExitKind.NATIVE_STATUS,
                    chain.status,
                ),
                SKIPPED,
            )
        }
        if (chain.uid != context.processUid() ||
            chain.pid != context.processPid() ||
            chain.echoedNonce != nonce
        ) {
            return DiagnosticRunResult(INTERNAL_UNKNOWN, SKIPPED)
        }

        val primitive = if (chain.nativeLogValid) SUCCESS else MALFORMED
        val dispatch = dispatchFact(chain.handoffStatus, chain.shellLogValid)
        return DiagnosticRunResult(primitive, dispatch)
    }

    override fun close() {
        if (!closed.compareAndSet(false, true)) return
        try {
            binding.close()
        } catch (problem: Throwable) {
            throw PluginException(
                PluginFailureCode.IO_FAILURE,
                "Diagnose binding close failed: ${problem::class.java.simpleName}",
            )
        }
    }

    private fun dispatchFact(handoffStatus: Int, shellLogValid: Boolean): PluginStageResult = when {
        handoffStatus == DiagnoseNativeBinding.HANDOFF_NOT_RUN -> SKIPPED
        handoffStatus == 0 && shellLogValid -> PluginStageResult(
            PluginStageState.SUCCESS,
            PluginResultReason.NONE,
            PluginExitKind.PROCESS_EXIT,
            0,
        )
        handoffStatus == 0 -> MALFORMED
        handoffStatus in 1..255 -> PluginStageResult(
            PluginStageState.FAILED,
            PluginResultReason.PROCESS_EXIT_NONZERO,
            PluginExitKind.PROCESS_EXIT,
            handoffStatus,
        )
        handoffStatus == DiagnoseNativeBinding.HANDOFF_CHILD_SIGNALED -> PluginStageResult(
            PluginStageState.FAILED,
            PluginResultReason.SIGNALLED,
            PluginExitKind.NATIVE_STATUS,
            handoffStatus,
        )
        handoffStatus in KNOWN_HELPER_FAILURES -> PluginStageResult(
            PluginStageState.FAILED,
            if (handoffStatus in ARTIFACT_HELPER_FAILURES) {
                PluginResultReason.ARTIFACT_IDENTITY_MISMATCH
            } else {
                PluginResultReason.IO_FAILURE
            },
            PluginExitKind.NATIVE_STATUS,
            handoffStatus,
        )
        else -> PluginStageResult(
            PluginStageState.UNKNOWN,
            PluginResultReason.INTERNAL_CONTRACT_FAILURE,
            PluginExitKind.NATIVE_STATUS,
            handoffStatus,
        )
    }

    private fun emit(
        context: DiagnosticRunContext,
        origin: PluginLogOrigin,
        level: PluginLogLevel,
        message: String,
    ) {
        runCatching { context.emit(origin, level, message) }
    }

    private companion object {
        val SUCCESS = PluginStageResult(
            PluginStageState.SUCCESS,
            PluginResultReason.NONE,
            PluginExitKind.NONE,
            0,
        )
        val SKIPPED = PluginStageResult(
            PluginStageState.SKIPPED,
            PluginResultReason.NOT_STARTED,
            PluginExitKind.NONE,
            0,
        )
        val CANCELLED = PluginStageResult(
            PluginStageState.CANCELLED,
            PluginResultReason.CANCELLED,
            PluginExitKind.NONE,
            0,
        )
        val MALFORMED = PluginStageResult(
            PluginStageState.UNKNOWN,
            PluginResultReason.MALFORMED_RESULT,
            PluginExitKind.NONE,
            0,
        )
        val INTERNAL_UNKNOWN = PluginStageResult(
            PluginStageState.UNKNOWN,
            PluginResultReason.INTERNAL_CONTRACT_FAILURE,
            PluginExitKind.NONE,
            0,
        )
        val ARTIFACT_HELPER_FAILURES = setOf(
            DiagnoseNativeBinding.HANDOFF_SCRIPT_TYPE,
            DiagnoseNativeBinding.HANDOFF_SCRIPT_MAGIC,
        )
        val KNOWN_HELPER_FAILURES = setOf(
            DiagnoseNativeBinding.HANDOFF_HOME_MISSING,
            DiagnoseNativeBinding.HANDOFF_HOME_INVALID,
            DiagnoseNativeBinding.HANDOFF_OPEN_HOME,
            DiagnoseNativeBinding.HANDOFF_OPEN_DIRECTORY,
            DiagnoseNativeBinding.HANDOFF_OPEN_SCRIPT,
            DiagnoseNativeBinding.HANDOFF_SCRIPT_TYPE,
            DiagnoseNativeBinding.HANDOFF_SCRIPT_MAGIC,
            DiagnoseNativeBinding.HANDOFF_SPAWN_CALL,
            DiagnoseNativeBinding.HANDOFF_WAIT,
            DiagnoseNativeBinding.HANDOFF_CHILD_SIGNALED,
            DiagnoseNativeBinding.HANDOFF_INTERNAL,
        )
    }
}
