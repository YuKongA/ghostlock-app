package com.ghostlock.app.backend.transport

import com.ghostlock.app.run.RunHostSnapshotReply

/**
 * Stable snapshot reply invariant used by App and Shizuku clients.
 *
 * @property code Synchronous host code.
 * @property snapshot Non-null exactly when [code] is [RunHostCode.OK].
 */
data class RunHostSnapshotReplyModel(
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
            val snapshot = wire.snapshot?.let(RunHostSnapshotModel::fromWire)
            if ((code == RunHostCode.OK) != (snapshot != null)) {
                return RunHostSnapshotReplyModel(RunHostCode.INTERNAL_CONTRACT_FAILURE, null)
            }
            return RunHostSnapshotReplyModel(code, snapshot)
        }
    }
}

/** Shared typed client outcome for App and Shizuku transports. */
sealed interface RunHostClientOutcome {
    /** Host published a decoded immutable terminal snapshot. */
    data class Terminal(val snapshot: RunHostSnapshotModel) : RunHostClientOutcome

    /** A synchronous lifecycle method rejected the run. */
    data class Rejected(val code: RunHostCode) : RunHostClientOutcome

    /** Binder died or the typed wire violated its invariant. */
    data class TransportFailure(val code: RunHostCode) : RunHostClientOutcome
}
