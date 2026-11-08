package com.ghostlock.app.data.runhost.wire

import android.os.Parcel
import android.os.Parcelable
import com.ghostlock.app.core.runhost.RunHostCode
import com.ghostlock.app.core.runhost.RunHostProtocol
import com.ghostlock.app.core.runhost.RunHostSnapshotModel
import com.ghostlock.app.core.runhost.RunHostState
import com.ghostlock.app.domain.run.DiagnosticOutcome
import com.ghostlock.app.domain.run.ExitKind
import com.ghostlock.app.domain.run.ResultReason
import com.ghostlock.app.domain.run.RunCapability
import com.ghostlock.app.domain.run.RunTerminal
import com.ghostlock.app.domain.run.StageResult
import com.ghostlock.app.domain.run.StageState
import com.ghostlock.app.run.RunHostSnapshot

/**
 * Measures the actual Parcelable data size without marshalling or closing embedded descriptors.
 *
 * @return Parcel data bytes written by the generated Parcelable implementation.
 */
internal fun Parcelable.parcelDataSize(): Int {
    val parcel = Parcel.obtain()
    return try {
        writeToParcel(parcel, 0)
        parcel.dataSize()
    } finally {
        parcel.recycle()
    }
}

/**
 * Encodes a snapshot under the fixed wire budget, falling back to a bounded typed unknown result.
 *
 * @return Generated wire snapshot whose measured Parcel size is within the protocol limit.
 */
internal fun RunHostSnapshotModel.toBoundedWire(): RunHostSnapshot {
    val candidate = toWire()
    if (candidate.parcelDataSize() <= RunHostProtocol.MAX_SNAPSHOT_PARCEL_BYTES) return candidate
    val fallback = boundedUnknownSnapshot().toWire()
    check(fallback.parcelDataSize() <= RunHostProtocol.MAX_SNAPSHOT_PARCEL_BYTES) {
        "bounded RunHost snapshot exceeds protocol budget"
    }
    return fallback
}

/** Returns a list-free typed fallback while preserving the established lifecycle and capability branch. */
private fun RunHostSnapshotModel.boundedUnknownSnapshot(): RunHostSnapshotModel {
    val unknown = StageResult(
        state = StageState.UNKNOWN,
        reason = ResultReason.INTERNAL_CONTRACT_FAILURE,
        exitKind = ExitKind.NONE,
        exitValue = 0,
    )
    if (state !in setOf(RunHostState.TERMINAL, RunHostState.DESTROYED) || terminal == RunTerminal.NONE) {
        return copy(
            lastCode = RunHostCode.INTERNAL_CONTRACT_FAILURE,
            backendPrimitive = StageResult.NOT_RUN,
            scriptDispatch = StageResult.NOT_RUN,
            payloadItems = emptyList(),
            payloadOverall = if (runCapability == RunCapability.DIAGNOSTIC_ONLY) {
                StageResult.SKIPPED
            } else {
                StageResult.NOT_RUN
            },
            finishItems = emptyList(),
            finishOverall = if (runCapability == RunCapability.DIAGNOSTIC_ONLY) {
                StageResult.SKIPPED
            } else {
                StageResult.NOT_RUN
            },
            backendCleanup = StageResult.NOT_RUN,
        )
    }
    return when (runCapability) {
        RunCapability.PRODUCT_ROOT -> copy(
            lastCode = RunHostCode.INTERNAL_CONTRACT_FAILURE,
            diagnosticOutcome = DiagnosticOutcome.NOT_APPLICABLE,
            backendPrimitive = unknown,
            scriptDispatch = unknown,
            payloadItems = emptyList(),
            payloadOverall = unknown,
            finishItems = emptyList(),
            finishOverall = unknown,
            backendCleanup = unknown,
            terminal = RunTerminal.UNKNOWN,
        )

        RunCapability.DIAGNOSTIC_ONLY -> copy(
            lastCode = RunHostCode.INTERNAL_CONTRACT_FAILURE,
            diagnosticOutcome = DiagnosticOutcome.UNKNOWN,
            backendPrimitive = unknown,
            scriptDispatch = StageResult.SKIPPED,
            payloadItems = emptyList(),
            payloadOverall = StageResult.SKIPPED,
            finishItems = emptyList(),
            finishOverall = StageResult.SKIPPED,
            backendCleanup = unknown,
            terminal = RunTerminal.UNKNOWN,
        )

        RunCapability.UNRESOLVED -> copy(
            lastCode = RunHostCode.INTERNAL_CONTRACT_FAILURE,
            payloadItems = emptyList(),
            finishItems = emptyList(),
        )
    }
}
