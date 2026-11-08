package com.ghostlock.app.data.runhost.wire

import com.ghostlock.app.core.runhost.RunHostCode
import com.ghostlock.app.core.runhost.RunHostIdentity
import com.ghostlock.app.core.runhost.RunHostProtocol
import com.ghostlock.app.core.runhost.RunHostSnapshotModel
import com.ghostlock.app.core.runhost.RunHostState
import com.ghostlock.app.domain.run.DiagnosticOutcome
import com.ghostlock.app.domain.run.ExitKind
import com.ghostlock.app.domain.run.FinishItemResult
import com.ghostlock.app.domain.run.ItemState
import com.ghostlock.app.domain.run.PayloadItemResult
import com.ghostlock.app.domain.run.ResultReason
import com.ghostlock.app.domain.run.RunCapability
import com.ghostlock.app.domain.run.RunTerminal
import com.ghostlock.app.domain.run.StageResult
import com.ghostlock.app.domain.run.StageState
import com.ghostlock.app.run.ItemResultParcel
import com.ghostlock.app.run.RunHostSnapshot
import com.ghostlock.app.run.RunHostSnapshotReply
import com.ghostlock.app.run.StageResultParcel

/**
 * Stable snapshot reply invariant used by App and Shizuku clients.
 *
 * @property code Synchronous host code.
 * @property snapshot Non-null exactly when [code] is [RunHostCode.OK].
 */
internal data class RunHostSnapshotReplyModel(
    val code: RunHostCode,
    val snapshot: RunHostSnapshotModel?,
) {
    init {
        require((code == RunHostCode.OK) == (snapshot != null)) {
            "snapshot must exist exactly for an OK reply"
        }
    }

    companion object {
        /** Decodes and enforces the OK/non-null snapshot invariant. */
        fun fromWire(wire: RunHostSnapshotReply): RunHostSnapshotReplyModel {
            val code = RunHostCode.fromWireValue(wire.code)
            val snapshot = wire.snapshot?.toModel()
            if ((code == RunHostCode.OK) != (snapshot != null)) {
                return RunHostSnapshotReplyModel(RunHostCode.INTERNAL_CONTRACT_FAILURE, null)
            }
            return RunHostSnapshotReplyModel(code, snapshot)
        }
    }
}

/**
 * Decodes one generated AIDL snapshot and fails closed on unknown stable values.
 *
 * @return Decoded immutable snapshot.
 * @throws IllegalArgumentException When a required value or conjunction is malformed.
 */
internal fun RunHostSnapshot.toModel(): RunHostSnapshotModel {
    require(parcelDataSize() <= RunHostProtocol.MAX_SNAPSHOT_PARCEL_BYTES) {
        "snapshot exceeds wire budget"
    }
    return RunHostSnapshotModel(
        identity = RunHostIdentity(runId, generation),
        state = requireNotNull(RunHostState.fromWireValue(state)) { "unknown host state" },
        lastCode = RunHostCode.fromWireValue(lastCode),
        runCapability = requireNotNull(RunCapability.fromWireValue(runCapability)) {
            "unknown run capability"
        },
        diagnosticOutcome = requireNotNull(DiagnosticOutcome.fromWireValue(diagnosticOutcome)) {
            "unknown diagnostic outcome"
        },
        backendPrimitive = backendPrimitive.toModel(),
        scriptDispatch = scriptDispatch.toModel(),
        payloadItems = payloadItems.map(ItemResultParcel::toPayloadModel),
        payloadOverall = payloadOverall.toModel(),
        finishItems = finishItems.map(ItemResultParcel::toFinishModel),
        finishOverall = finishOverall.toModel(),
        backendCleanup = backendCleanup.toModel(),
        terminal = requireNotNull(RunTerminal.fromWireValue(terminal)) { "unknown terminal" },
        logSequence = logSequence,
        droppedLogs = droppedLogs,
    )
}

/** Encodes an immutable in-process snapshot to the generated AIDL Parcelable. */
internal fun RunHostSnapshotModel.toWire(): RunHostSnapshot = RunHostSnapshot().also { parcel ->
    parcel.runId = identity.runId
    parcel.generation = identity.generation
    parcel.state = state.wireValue
    parcel.lastCode = lastCode.wireValue
    parcel.runCapability = runCapability.wireValue
    parcel.diagnosticOutcome = diagnosticOutcome.wireValue
    parcel.backendPrimitive = backendPrimitive.toWire()
    parcel.scriptDispatch = scriptDispatch.toWire()
    parcel.payloadItems = payloadItems.map(PayloadItemResult::toWire)
    parcel.payloadOverall = payloadOverall.toWire()
    parcel.finishItems = finishItems.map(FinishItemResult::toWire)
    parcel.finishOverall = finishOverall.toWire()
    parcel.backendCleanup = backendCleanup.toWire()
    parcel.terminal = terminal.wireValue
    parcel.logSequence = logSequence
    parcel.droppedLogs = droppedLogs
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

/** Encodes one stable stage fact. */
private fun StageResult.toWire(): StageResultParcel = StageResultParcel().also { parcel ->
    parcel.state = state.wireValue
    parcel.reason = reason.wireValue
    parcel.exitKind = exitKind.wireValue
    parcel.exitValue = exitValue
}

/** Encodes one Payload item fact. */
private fun PayloadItemResult.toWire(): ItemResultParcel = ItemResultParcel().also { parcel ->
    parcel.itemId = itemId
    parcel.state = state.wireValue
    parcel.reason = reason.wireValue
    parcel.exitKind = exitKind.wireValue
    parcel.exitValue = exitValue
}

/** Encodes one Finish item fact. */
private fun FinishItemResult.toWire(): ItemResultParcel = ItemResultParcel().also { parcel ->
    parcel.itemId = itemId
    parcel.state = state.wireValue
    parcel.reason = reason.wireValue
    parcel.exitKind = exitKind.wireValue
    parcel.exitValue = exitValue
}
