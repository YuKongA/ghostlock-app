package com.ghostlock.app.data.runhost.wire

import android.os.SystemClock
import com.ghostlock.app.core.runhost.RunHostCode
import com.ghostlock.app.core.runhost.RunHostController
import com.ghostlock.app.core.runhost.RunHostEventSink
import com.ghostlock.app.core.runhost.RunHostIdentity
import com.ghostlock.app.core.runhost.RunHostProtocol
import com.ghostlock.app.core.runhost.RunHostSnapshotModel
import com.ghostlock.app.data.runhost.host.AndroidRunHostPreparationAdapter
import com.ghostlock.app.domain.run.LogDraft
import com.ghostlock.app.run.IRunHostCallback
import com.ghostlock.app.run.RunHostLogRecord
import com.ghostlock.app.run.RunHostRequest
import com.ghostlock.app.run.RunHostSnapshotReply

/** Shared AIDL/Parcel endpoint that delegates all business state to one controller. */
internal class RunHostBinderEndpoint(
    private val controller: RunHostController,
    private val preparation: AndroidRunHostPreparationAdapter,
) {
    fun setCallback(
        runId: String,
        generation: Long,
        callback: IRunHostCallback,
    ): Int {
        val identity = identityOrNull(runId, generation) ?: return RunHostCode.INVALID_REQUEST.wireValue
        return controller.setCallback(identity, BinderEventSink(callback)).wireValue
    }

    fun prepare(request: RunHostRequest): Int {
        return try {
            val parcelBytes = runCatching { request.parcelDataSize() }
                .getOrElse { return RunHostCode.INVALID_REQUEST.wireValue }
            if (parcelBytes > RunHostProtocol.MAX_REQUEST_PARCEL_BYTES) {
                return RunHostCode.LIMIT_EXCEEDED.wireValue
            }
            val model = runCatching { request.toModel() }
                .getOrElse { return RunHostCode.INVALID_REQUEST.wireValue }
            controller.prepare(model) { normalized, identity ->
                preparation.materialize(normalized, identity) {
                    request.duplicateDescriptors(normalized)
                }
            }.wireValue
        } finally {
            request.closeDecodedDescriptors()
        }
    }

    fun start(runId: String, generation: Long): Int {
        val identity = identityOrNull(runId, generation) ?: return RunHostCode.INVALID_REQUEST.wireValue
        return controller.start(identity).wireValue
    }

    fun getSnapshot(runId: String, generation: Long): RunHostSnapshotReply {
        val identity = identityOrNull(runId, generation)
            ?: return reply(RunHostCode.INVALID_REQUEST, null)
        val result = controller.snapshot(identity)
        if (result.code != RunHostCode.OK) return reply(result.code, null)
        val bounded = runCatching { checkNotNull(result.snapshot).toBoundedWire() }
            .getOrElse { return reply(RunHostCode.INTERNAL_CONTRACT_FAILURE, null) }
        return RunHostSnapshotReply().also { wire ->
            wire.code = RunHostCode.OK.wireValue
            wire.snapshot = bounded
        }
    }

    fun cancel(runId: String, generation: Long): Int {
        val identity = identityOrNull(runId, generation) ?: return RunHostCode.INVALID_REQUEST.wireValue
        return controller.cancel(identity).wireValue
    }

    fun requestClose(runId: String, generation: Long): Int {
        val identity = identityOrNull(runId, generation) ?: return RunHostCode.INVALID_REQUEST.wireValue
        return controller.requestClose(identity).wireValue
    }

    /** Parses a compound identity without throwing through Binder. */
    private fun identityOrNull(runId: String, generation: Long): RunHostIdentity? =
        runCatching { RunHostIdentity(runId, generation) }.getOrNull()

    /** Returns one generated reply with the stable OK/non-null invariant. */
    private fun reply(code: RunHostCode, model: RunHostSnapshotModel?): RunHostSnapshotReply =
        RunHostSnapshotReply().also { wire ->
            wire.code = code.wireValue
            wire.snapshot = model?.toBoundedWire()
        }

    /** Adapts one generated Binder callback to the transport-neutral event sink. */
    private class BinderEventSink(
        private val callback: IRunHostCallback,
    ) : RunHostEventSink {
        override fun publishLog(
            identity: RunHostIdentity,
            sequence: Long,
            draft: LogDraft,
        ): Boolean = runCatching {
            val record = RunHostLogRecord().also { wire ->
                wire.runId = identity.runId
                wire.generation = identity.generation
                wire.sequence = sequence
                wire.elapsedRealtimeMs = SystemClock.elapsedRealtime()
                wire.origin = draft.origin.wireValue
                wire.level = draft.level.wireValue
                wire.message = draft.message
            }
            if (record.parcelDataSize() > RunHostProtocol.MAX_LOG_PARCEL_BYTES) return false
            callback.onLog(record)
            true
        }.getOrDefault(false)

        override fun publishTerminal(snapshot: RunHostSnapshotModel): Boolean = runCatching {
            callback.onTerminal(snapshot.toBoundedWire())
            true
        }.getOrDefault(false)
    }
}
