package com.ghostlock.app.backend.transport

import com.ghostlock.app.domain.run.DiagnosticOutcome
import com.ghostlock.app.domain.run.FinishItemResult
import com.ghostlock.app.domain.run.PayloadItemResult
import com.ghostlock.app.domain.run.RunCapability
import com.ghostlock.app.domain.run.RunReport
import com.ghostlock.app.domain.run.RunTerminal
import com.ghostlock.app.domain.run.StageResult
import com.ghostlock.app.domain.run.ExitKind
import com.ghostlock.app.domain.run.ItemState
import com.ghostlock.app.domain.run.ResultReason
import com.ghostlock.app.domain.run.StageState
import com.ghostlock.app.run.ItemResultParcel
import com.ghostlock.app.run.RunHostSnapshot
import com.ghostlock.app.run.StageResultParcel

/**
 * Immutable in-process representation of the typed RunHost snapshot.
 *
 * @property identity Compound run identity.
 * @property state Host lifecycle state.
 * @property lastCode Most recent stable synchronous code.
 * @property runCapability Registry-projected capability.
 * @property diagnosticOutcome Independent diagnostic result.
 * @property backendPrimitive Primitive or fixture stage.
 * @property scriptDispatch Exact root-script dispatch stage.
 * @property payloadItems Ordered Payload facts.
 * @property payloadOverall Aggregate Payload fact.
 * @property finishItems Ordered Finish facts.
 * @property finishOverall Aggregate Finish fact.
 * @property backendCleanup Backend safety cleanup fact.
 * @property terminal Stable terminal.
 * @property logSequence Last sequence reserved for a callback delivery attempt.
 * @property droppedLogs Host-side validation, budget, parcel, or delivery drop count.
 */
data class RunHostSnapshotModel(
    val identity: RunHostIdentity,
    val state: RunHostState,
    val lastCode: RunHostCode,
    val runCapability: RunCapability,
    val diagnosticOutcome: DiagnosticOutcome,
    val backendPrimitive: StageResult,
    val scriptDispatch: StageResult,
    val payloadItems: List<PayloadItemResult>,
    val payloadOverall: StageResult,
    val finishItems: List<FinishItemResult>,
    val finishOverall: StageResult,
    val backendCleanup: StageResult,
    val terminal: RunTerminal,
    val logSequence: Long,
    val droppedLogs: Long,
) {
    init {
        require(logSequence >= 0L) { "log sequence is negative" }
        require(logSequence <= RunHostProtocol.MAX_QUEUED_LOGS.toLong()) { "log sequence exceeds budget" }
        require(droppedLogs >= 0L) { "dropped log count is negative" }
        require(backendPrimitive.isValidStageResult()) { "invalid backend primitive result" }
        require(scriptDispatch.isValidStageResult()) { "invalid script dispatch result" }
        require(payloadOverall.isValidStageResult()) { "invalid Payload aggregate result" }
        require(finishOverall.isValidStageResult()) { "invalid Finish aggregate result" }
        require(backendCleanup.isValidStageResult()) { "invalid backend cleanup result" }
        require(payloadItems.size <= RunHostProtocol.MAX_PAYLOAD_ITEMS) { "too many Payload results" }
        require(finishItems.size <= RunHostProtocol.MAX_FINISH_ITEMS) { "too many Finish results" }
        require(payloadItems.all { item -> item.isValidItemResult() }) { "invalid Payload item result" }
        require(finishItems.all { item -> item.isValidItemResult() }) { "invalid Finish item result" }
        val itemIds = payloadItems.map(PayloadItemResult::itemId) + finishItems.map(FinishItemResult::itemId)
        require(itemIds.all(RunHostProtocol::isLogicalId) && itemIds.size == itemIds.toSet().size) {
            "invalid or duplicate item result identity"
        }
        require(isValidCapabilityTerminalTuple()) { "invalid capability/outcome/terminal tuple" }
        if (runCapability == RunCapability.DIAGNOSTIC_ONLY) {
            require(payloadItems.isEmpty() && finishItems.isEmpty()) { "diagnostic run contains product items" }
            require(payloadOverall == StageResult.SKIPPED && finishOverall == StageResult.SKIPPED) {
                "diagnostic run contains product aggregate facts"
            }
        }
    }

    /** Returns whether lifecycle, capability, diagnostic outcome, and terminal obey the fixed wire table. */
    private fun isValidCapabilityTerminalTuple(): Boolean = when (state) {
        RunHostState.NEW ->
            runCapability == RunCapability.UNRESOLVED &&
                diagnosticOutcome == DiagnosticOutcome.NOT_RUN && terminal == RunTerminal.NONE

        RunHostState.PREPARED, RunHostState.RUNNING -> terminal == RunTerminal.NONE && when (runCapability) {
            RunCapability.UNRESOLVED -> false
            RunCapability.PRODUCT_ROOT -> diagnosticOutcome == DiagnosticOutcome.NOT_APPLICABLE
            RunCapability.DIAGNOSTIC_ONLY -> diagnosticOutcome == DiagnosticOutcome.NOT_RUN
        }

        RunHostState.TERMINAL -> isValidTerminalTuple()
        RunHostState.DESTROYED -> if (terminal == RunTerminal.NONE) {
            when (runCapability) {
                RunCapability.UNRESOLVED -> diagnosticOutcome == DiagnosticOutcome.NOT_RUN
                RunCapability.PRODUCT_ROOT -> diagnosticOutcome == DiagnosticOutcome.NOT_APPLICABLE
                RunCapability.DIAGNOSTIC_ONLY -> diagnosticOutcome == DiagnosticOutcome.NOT_RUN
            }
        } else {
            isValidTerminalTuple()
        }
    }

    /** Returns whether an immutable terminal tuple is one of the protocol-authorized combinations. */
    private fun isValidTerminalTuple(): Boolean = when (runCapability) {
        RunCapability.UNRESOLVED ->
            diagnosticOutcome == DiagnosticOutcome.NOT_RUN && terminal == RunTerminal.CANCELLED

        RunCapability.PRODUCT_ROOT ->
            diagnosticOutcome == DiagnosticOutcome.NOT_APPLICABLE && terminal in setOf(
                RunTerminal.SUCCESS,
                RunTerminal.DEGRADED,
                RunTerminal.FAILED,
                RunTerminal.CANCELLED,
                RunTerminal.UNKNOWN,
                RunTerminal.NEEDS_ATTENTION,
                RunTerminal.REBOOT_REQUIRED,
                RunTerminal.REBOOT_REQUESTED,
            )

        RunCapability.DIAGNOSTIC_ONLY -> when (diagnosticOutcome) {
            DiagnosticOutcome.PASS, DiagnosticOutcome.FAIL -> terminal == RunTerminal.DIAGNOSTIC_COMPLETE
            DiagnosticOutcome.NOT_RUN -> terminal == RunTerminal.CANCELLED
            DiagnosticOutcome.UNKNOWN -> terminal in setOf(RunTerminal.UNKNOWN, RunTerminal.CANCELLED)
            DiagnosticOutcome.NOT_APPLICABLE -> false
        }
    }

    /** Projects the immutable six-layer report without reading log text. */
    fun toRunReport(): RunReport = RunReport(
        runCapability = runCapability,
        diagnosticOutcome = diagnosticOutcome,
        backendPrimitive = backendPrimitive,
        rootScriptDispatch = scriptDispatch,
        payloadItems = payloadItems,
        payloadOverall = payloadOverall,
        finishItems = finishItems,
        finishOverall = finishOverall,
        backendCleanup = backendCleanup,
        terminal = terminal,
    )

    companion object {
        /**
         * Decodes one generated AIDL snapshot and fails closed on unknown identities or stable values.
         *
         * @param wire Generated typed wire object.
         * @return Decoded immutable snapshot.
         * @throws IllegalArgumentException When a required stable value is unknown or malformed.
         */
        fun fromWire(wire: RunHostSnapshot): RunHostSnapshotModel {
            require(wire.parcelDataSize() <= RunHostProtocol.MAX_SNAPSHOT_PARCEL_BYTES) {
                "snapshot exceeds wire budget"
            }
            val identity = RunHostIdentity(wire.runId, wire.generation)
            return RunHostSnapshotModel(
                identity = identity,
                state = requireNotNull(RunHostState.fromWireValue(wire.state)) { "unknown host state" },
                lastCode = RunHostCode.fromWireValue(wire.lastCode),
                runCapability = requireNotNull(RunCapability.fromWireValue(wire.runCapability)) {
                    "unknown run capability"
                },
                diagnosticOutcome = requireNotNull(DiagnosticOutcome.fromWireValue(wire.diagnosticOutcome)) {
                    "unknown diagnostic outcome"
                },
                backendPrimitive = wire.backendPrimitive.toModel(),
                scriptDispatch = wire.scriptDispatch.toModel(),
                payloadItems = wire.payloadItems.map(ItemResultParcel::toPayloadModel),
                payloadOverall = wire.payloadOverall.toModel(),
                finishItems = wire.finishItems.map(ItemResultParcel::toFinishModel),
                finishOverall = wire.finishOverall.toModel(),
                backendCleanup = wire.backendCleanup.toModel(),
                terminal = requireNotNull(RunTerminal.fromWireValue(wire.terminal)) { "unknown terminal" },
                logSequence = wire.logSequence,
                droppedLogs = wire.droppedLogs,
            )
        }
    }
}

/** Returns whether one stage result obeys the stable state/reason/exit conjunction matrix. */
private fun StageResult.isValidStageResult(): Boolean {
    val noExit = exitKind == ExitKind.NONE && exitValue == 0
    val processSuccess = exitKind == ExitKind.PROCESS_EXIT && exitValue == 0
    val nativeSuccess = exitKind == ExitKind.NATIVE_STATUS && exitValue == 0
    return when (state) {
        StageState.NOT_RUN, StageState.SKIPPED -> reason == ResultReason.NOT_STARTED && noExit
        StageState.RUNNING -> reason == ResultReason.NONE && noExit
        StageState.SUCCESS -> reason == ResultReason.NONE && (noExit || processSuccess || nativeSuccess)
        StageState.CANCELLED -> reason == ResultReason.CANCELLED && noExit
        StageState.UNKNOWN -> reason in setOf(
            ResultReason.HOST_DIED,
            ResultReason.MALFORMED_RESULT,
            ResultReason.INTERNAL_CONTRACT_FAILURE,
            ResultReason.ACK_MISSING,
        ) && noExit

        StageState.FAILED -> when (reason) {
            ResultReason.PROCESS_EXIT_NONZERO ->
                exitKind == ExitKind.PROCESS_EXIT && exitValue in 1..255

            ResultReason.SIGNALLED -> exitKind == ExitKind.SIGNAL && exitValue in 1..127
            ResultReason.REJECTED -> noExit || (exitKind == ExitKind.NATIVE_STATUS && exitValue != 0)
            ResultReason.NOT_READY,
            ResultReason.IO_FAILURE,
            ResultReason.TIMEOUT,
            ResultReason.ARTIFACT_IDENTITY_MISMATCH,
            ResultReason.INTERNAL_CONTRACT_FAILURE,
            ResultReason.CLEANUP_INCOMPLETE
            -> noExit

            else -> false
        }
    }
}

/** Returns whether one Payload item result obeys the stable item conjunction matrix. */
private fun PayloadItemResult.isValidItemResult(): Boolean =
    isValidItemResult(itemId, state, reason, exitKind, exitValue)

/** Returns whether one Finish item result obeys the stable item conjunction matrix. */
private fun FinishItemResult.isValidItemResult(): Boolean =
    isValidItemResult(itemId, state, reason, exitKind, exitValue)

/** Shared item state/reason/exit validator for Payload and Finish results. */
private fun isValidItemResult(
    itemId: String,
    state: ItemState,
    reason: ResultReason,
    exitKind: ExitKind,
    exitValue: Int,
): Boolean {
    if (!RunHostProtocol.isLogicalId(itemId)) return false
    val noExit = exitKind == ExitKind.NONE && exitValue == 0
    val processSuccess = exitKind == ExitKind.PROCESS_EXIT && exitValue == 0
    return when (state) {
        ItemState.NOT_RUN, ItemState.SKIPPED_DEPENDENCY -> reason == ResultReason.NOT_STARTED && noExit
        ItemState.DISPATCHING -> reason == ResultReason.NONE && noExit
        ItemState.SUCCESS -> reason == ResultReason.NONE && (noExit || processSuccess)
        ItemState.CANCELLED -> reason == ResultReason.CANCELLED && noExit
        ItemState.UNKNOWN -> reason in setOf(
            ResultReason.HOST_DIED,
            ResultReason.MALFORMED_RESULT,
            ResultReason.INTERNAL_CONTRACT_FAILURE,
            ResultReason.ACK_MISSING,
        ) && noExit

        ItemState.FAILED -> when (reason) {
            ResultReason.PROCESS_EXIT_NONZERO ->
                exitKind == ExitKind.PROCESS_EXIT && exitValue in 1..255

            ResultReason.SIGNALLED -> exitKind == ExitKind.SIGNAL && exitValue in 1..127
            ResultReason.REJECTED,
            ResultReason.IO_FAILURE,
            ResultReason.TIMEOUT
            -> noExit

            else -> false
        }
    }
}

/** Decodes one stable stage result. */
private fun StageResultParcel.toModel(): StageResult = StageResult(
    state = requireNotNull(StageState.fromWireValue(state)) { "unknown stage state" },
    reason = requireNotNull(ResultReason.fromWireValue(reason)) { "unknown stage reason" },
    exitKind = requireNotNull(ExitKind.fromWireValue(exitKind)) { "unknown stage exit kind" },
    exitValue = exitValue,
)

/** Decodes one Payload item result. */
private fun ItemResultParcel.toPayloadModel(): PayloadItemResult = PayloadItemResult(
    itemId = itemId,
    state = requireNotNull(ItemState.fromWireValue(state)) { "unknown Payload item state" },
    reason = requireNotNull(ResultReason.fromWireValue(reason)) { "unknown Payload item reason" },
    exitKind = requireNotNull(ExitKind.fromWireValue(exitKind)) { "unknown Payload item exit kind" },
    exitValue = exitValue,
)

/** Decodes one Finish item result. */
private fun ItemResultParcel.toFinishModel(): FinishItemResult = FinishItemResult(
    itemId = itemId,
    state = requireNotNull(ItemState.fromWireValue(state)) { "unknown Finish item state" },
    reason = requireNotNull(ResultReason.fromWireValue(reason)) { "unknown Finish item reason" },
    exitKind = requireNotNull(ExitKind.fromWireValue(exitKind)) { "unknown Finish item exit kind" },
    exitValue = exitValue,
)
