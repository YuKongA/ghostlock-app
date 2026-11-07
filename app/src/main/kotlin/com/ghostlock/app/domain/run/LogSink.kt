package com.ghostlock.app.domain.run

import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.Deferred

/** Frozen identity and automatic-export choice for one accepted log document. */
data class LogSinkRequest(
    val enabled: Boolean,
    val runId: String,
    val generation: Long,
    val backendKey: String,
    val relativePath: String,
    val formatConfig: LogFormatConfig,
) {
    init {
        require(runId.isNotBlank()) { "run id must not be blank" }
        require(generation > 0L) { "run generation must be positive" }
        require(backendKey.isNotBlank()) { "backend key must not be blank" }
        require(relativePath.isNotBlank()) { "export path must not be blank" }
    }
}

/** First-failure categories; none changes the typed [RunReport]. */
enum class LogSinkFailureKind { OPEN, WRITE, FLUSH, CLOSE }

/** Storage-neutral first-failure diagnostic. */
data class LogSinkFailure(val kind: LogSinkFailureKind, val detail: String)

/** Stable private-file prefix after all preceding rows have been flushed. */
data class LogFilePrefix(val path: String, val byteLength: Long)

/** Immutable processed event shared by the dispatcher and each independent sink queue. */
sealed interface LogSinkEvent {
    /** Header rows for one document; consumers must not modify the shared bytes. */
    data class Start(val rows: List<ByteArray>) : LogSinkEvent

    /** One committed row and its sole formatter output. */
    data class Entry(val record: LogRecord, val row: ByteArray) : LogSinkEvent

    /** UI-only clear at its position in the raw/processed FIFO. */
    data object UiClear : LogSinkEvent

    /** Private-file-only flush and exact-prefix snapshot. */
    data class FileBarrier(val completion: CompletableDeferred<LogFilePrefix>) : LogSinkEvent

    /** Final session/end rows; this marker does not certify durable storage. */
    data class End(val rows: List<ByteArray>) : LogSinkEvent
}

/** One nonblocking sink producer; platform I/O belongs exclusively to its worker. */
interface LogSink {
    /** @return True when [event] entered this sink's unlimited independent queue. */
    fun tryEmit(event: LogSinkEvent): Boolean

    /** Fails pending completions and stops this sink if the stream runtime terminates unexpectedly. */
    fun abort(reason: String)
}

/** Always-present private-file sink with terminal drain evidence. */
interface LogFileSink : LogSink {
    val finished: Deferred<LogFilePrefix>
}

/** The three current sink destinations; routing controls target one role, while rows reach all active roles. */
enum class LogSinkRole { UI, PRIVATE_FILE, MEDIA_STORE }

/** One run-owned sink and its routing role. */
data class LogSinkBinding(val role: LogSinkRole, val sink: LogSink)
