package com.ghostlock.app.domain.run

/** Renderer-neutral log severity with an explicit stable RunHost wire value. */
enum class RunLogLevel(val wireValue: Int) {
    Verbose(1),
    Debug(2),
    Info(3),
    Warn(4),
    Error(5),
    ;

    companion object {
        /** @return Exact stable value, or null for an unknown wire integer. */
        fun fromWireValue(value: Int): RunLogLevel? = entries.firstOrNull { level ->
            level.wireValue == value
        }
    }
}

/** Renderer-neutral component family with an explicit stable RunHost wire value. */
enum class RunLogOrigin(val wireValue: Int) {
    Kotlin(1),
    Native(2),
    Shell(3),
    ;

    companion object {
        /** @return Exact stable value, or null for an unknown wire integer. */
        fun fromWireValue(value: Int): RunLogOrigin? = entries.firstOrNull { origin ->
            origin.wireValue == value
        }
    }
}

/**
 * Producer-side log message before RunHost identity, sequence, and monotonic metadata are stamped.
 *
 * @property origin Component family that produced the message.
 * @property level Semantic severity used by every downstream sink.
 * @property message Bounded single-line ASCII body with only the approved SGR subset as control data.
 */
data class RunLogDraft(
    val origin: RunLogOrigin,
    val level: RunLogLevel,
    val message: String,
)

/**
 * Sole canonical record committed by the application log hub and consumed by every sink.
 *
 * @property origin Component family that produced the record.
 * @property level Semantic severity used by every downstream sink.
 * @property message Literal validated body without the presentation prefix.
 * @property elapsedMs Hub-relative non-negative elapsed time; zero for non-run records.
 * @note List position under the RunCoordinator lock is the sole global ordering authority.
 */
data class RunLogRecord(
    val origin: RunLogOrigin,
    val level: RunLogLevel,
    val message: String,
    val elapsedMs: Long = 0L,
)

/** Producer boundary supplied to one Flow; delivery failure never changes execution facts. */
fun interface RunLogEmitter {
    /**
     * Emits one immutable log draft for RunHost validation and sequencing.
     *
     * @param record Producer-owned immutable draft.
     */
    fun emit(record: RunLogDraft)
}

/** Read-only cancellation view supplied to one backend Flow. */
fun interface RunCancellationView {
    /** @return True when a user cancellation request has been recorded for this run. */
    fun isCancellationRequested(): Boolean
}
