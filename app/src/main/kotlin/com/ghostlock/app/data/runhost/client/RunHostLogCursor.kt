package com.ghostlock.app.data.runhost.client

import com.ghostlock.app.core.runhost.RunHostIdentity
import com.ghostlock.app.core.runhost.RunHostProtocol
import com.ghostlock.app.core.runhost.isValidRunHostLogMessage
import com.ghostlock.app.data.runhost.wire.parcelDataSize
import com.ghostlock.app.domain.run.LogDraft
import com.ghostlock.app.domain.run.LogLevel
import com.ghostlock.app.domain.run.LogOrigin
import com.ghostlock.app.run.RunHostLogRecord

/** Result of validating one source-private RunHost callback record. */
internal sealed interface RunHostLogCursorResult {
    /** One exact next record decoded to the common raw draft shape. */
    data class Accepted(val draft: LogDraft) : RunHostLogCursorResult

    /** Source delivery is incomplete from this record onward. */
    data class Rejected(val reason: String) : RunHostLogCursorResult
}
/**
 * Per-run source-delivery authority for one typed RunHost connector.
 *
 * @param identity Immutable run identity established before callback registration.
 * @param parcelSize Exact Android parcel-size reader; injectable only for host tests.
 * @note Source sequence detects gaps, duplicates, and inversions but never becomes global LogHub commit order.
 */
internal class RunHostLogCursor(
    private val identity: RunHostIdentity,
    private val parcelSize: (RunHostLogRecord) -> Int = RunHostLogRecord::parcelDataSize,
) {
    private val lock = Any()
    private var lastSequence: Long = 0L

    /**
     * Accepts one per-record-bounded current callback with a strictly increasing positive source sequence.
     *
     * @param record Generated typed wire record.
     * @return Accepted next draft, or the first source-integrity failure.
     */
    fun accept(record: RunHostLogRecord): RunHostLogCursorResult {
        if (runCatching { parcelSize(record) }.getOrDefault(Int.MAX_VALUE) >
            RunHostProtocol.MAX_LOG_PARCEL_BYTES
        ) {
            return RunHostLogCursorResult.Rejected("runhost-log-parcel-limit")
        }
        if (record.runId != identity.runId || record.generation != identity.generation) {
            return RunHostLogCursorResult.Rejected("runhost-log-identity")
        }
        if (record.sequence <= 0L) return RunHostLogCursorResult.Rejected("runhost-log-sequence")
        if (record.elapsedRealtimeMs < 0L) return RunHostLogCursorResult.Rejected("runhost-log-time")
        val origin = LogOrigin.fromWireValue(record.origin)
            ?: return RunHostLogCursorResult.Rejected("runhost-log-origin")
        val level = LogLevel.fromWireValue(record.level)
            ?: return RunHostLogCursorResult.Rejected("runhost-log-level")
        if (!isValidRunHostLogMessage(record.message)) {
            return RunHostLogCursorResult.Rejected("runhost-log-message")
        }
        return synchronized(lock) {
            if (lastSequence == Long.MAX_VALUE || record.sequence != lastSequence + 1L) {
                RunHostLogCursorResult.Rejected("runhost-log-sequence-gap")
            } else {
                lastSequence = record.sequence
                RunHostLogCursorResult.Accepted(
                    LogDraft(origin, level, record.message, record.elapsedRealtimeMs),
                )
            }
        }
    }

    /**
     * Verifies the terminal marker against the exact accepted source prefix.
     *
     * @return Null only when every reserved sequence was delivered and the host reported no drops.
     */
    fun terminalFailure(finalSequence: Long, droppedLogs: Long): String? = synchronized(lock) {
        when {
            finalSequence < 0L || droppedLogs < 0L -> "runhost-log-terminal-invalid"
            droppedLogs != 0L -> "runhost-log-host-dropped"
            finalSequence != lastSequence -> "runhost-log-terminal-gap"
            else -> null
        }
    }
}
