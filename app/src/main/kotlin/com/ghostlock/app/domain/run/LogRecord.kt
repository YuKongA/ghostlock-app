package com.ghostlock.app.domain.run

/** Renderer-neutral log severity with an explicit stable RunHost wire value. */
enum class LogLevel(val wireValue: Int) {
    Verbose(1), Debug(2), Info(3), Warn(4), Error(5);

    companion object {
        /** @return Exact stable value, or null for an unknown wire integer. */
        fun fromWireValue(value: Int): LogLevel? = entries.firstOrNull { it.wireValue == value }
    }
}

/** Renderer-neutral component family with an explicit stable RunHost wire value. */
enum class LogOrigin(val wireValue: Int) {
    Kotlin(1), Native(2), Shell(3);

    companion object {
        /** @return Exact stable value, or null for an unknown wire integer. */
        fun fromWireValue(value: Int): LogOrigin? = entries.firstOrNull { it.wireValue == value }
    }
}

/**
 * Producer message before LogHub assigns global commit order and run-relative time.
 *
 * @property sourceElapsedMs Optional source-local elapsed time retained only as diagnostic metadata.
 */
data class LogDraft(
    val origin: LogOrigin,
    val level: LogLevel,
    val message: String,
    val sourceElapsedMs: Long? = null,
) {
    init {
        require(sourceElapsedMs == null || sourceElapsedMs >= 0L) { "negative source elapsed time" }
    }
}

/** Immutable row committed in LogHub order; [commitId] is unique within its document. */
data class LogRecord(
    val origin: LogOrigin,
    val level: LogLevel,
    val message: String,
    val elapsedMs: Long = 0L,
    val commitId: Long = 0L,
    val sourceElapsedMs: Long? = null,
)

/** Producer boundary supplied to one Flow; delivery failure never changes execution facts. */
fun interface LogEmitter {
    /** Emits one immutable draft for RunHost validation and sequencing. */
    fun emit(record: LogDraft)
}

/** Read-only cancellation view supplied to one backend Flow. */
fun interface RunCancellationView {
    /** @return True when a user cancellation request has been recorded for this run. */
    fun isCancellationRequested(): Boolean
}
