package com.ghostlock.app.data.log

import android.os.ParcelFileDescriptor
import com.ghostlock.app.core.runhost.RunHostProtocol
import com.ghostlock.app.core.runhost.isValidRunHostLogMessage
import com.ghostlock.app.domain.run.LogConnector
import com.ghostlock.app.domain.run.LogConnectorEnd
import com.ghostlock.app.domain.run.LogDraft
import com.ghostlock.app.domain.run.LogLevel
import com.ghostlock.app.domain.run.LogOrigin
import java.io.Closeable
import java.io.InputStream
import java.util.concurrent.atomic.AtomicBoolean

/**
 * Streaming native/shell descriptor reader with source-local FIFO and no cumulative byte limit.
 *
 * Each physical line remains bounded by the existing RunHost per-record budget. A malformed, oversized, partial,
 * or failed stream stops accepting later records but still drains or closes the physical source before returning
 * [LogConnectorEnd.Incomplete].
 */
internal class FdLogConnector private constructor(
    private val input: InputStream,
    private val origin: LogOrigin,
) : LogConnector, Closeable {
    private val collecting = AtomicBoolean(false)
    private val closed = AtomicBoolean(false)

    /** Takes ownership of one read descriptor; the producer retains and closes its separate write endpoint. */
    constructor(descriptor: ParcelFileDescriptor, origin: LogOrigin) : this(
        ParcelFileDescriptor.AutoCloseInputStream(descriptor),
        origin,
    )

    /** Host-test seam that preserves the same parser and close ownership without Android descriptor plumbing. */
    internal constructor(source: InputStream, origin: LogOrigin, hostTest: Boolean) : this(source, origin) {
        check(hostTest) { "host-test constructor requires an explicit test marker" }
    }

    /** Reads through EOF, emitting only the complete valid prefix in physical line order. */
    override suspend fun collect(emit: suspend (LogDraft) -> Unit): LogConnectorEnd {
        check(collecting.compareAndSet(false, true)) { "FD log connector is already collecting" }
        val line = ByteArray(MAX_SOURCE_RECORD_BYTES)
        val chunk = ByteArray(DEFAULT_BUFFER_SIZE)
        var lineSize = 0
        var discarding = false
        var accepting = true
        var firstFailure: String? = null
        try {
            while (true) {
                val count = input.read(chunk)
                if (count < 0) break
                for (index in 0 until count) {
                    val byte = chunk[index]
                    if (byte == LF) {
                        if (discarding) {
                            firstFailure = firstFailure ?: "fd-line-overflow"
                            accepting = false
                        } else if (accepting) {
                            val draft = decodeLine(line, lineSize)
                            if (draft == null) {
                                firstFailure = firstFailure ?: "fd-line-malformed"
                                accepting = false
                            } else {
                                emit(draft)
                            }
                        }
                        lineSize = 0
                        discarding = false
                    } else if (!discarding) {
                        if (lineSize == line.size) {
                            discarding = true
                        } else {
                            line[lineSize] = byte
                            lineSize += 1
                        }
                    }
                }
            }
            if (discarding || lineSize != 0) {
                firstFailure = firstFailure ?: if (discarding) "fd-line-overflow" else "fd-partial-line"
            }
        } catch (problem: Throwable) {
            firstFailure = firstFailure ?: "fd-read-" + problem::class.java.simpleName
        }
        return firstFailure?.let(LogConnectorEnd::Incomplete) ?: LogConnectorEnd.Drained
    }

    /** Closes the read endpoint exactly once after drain, or early when the owning run aborts. */
    override fun close() {
        if (closed.compareAndSet(false, true)) runCatching { input.close() }
    }

    /** Decodes one complete LF-stripped source record while retaining source-local elapsed metadata only. */
    private fun decodeLine(bytes: ByteArray, size: Int): LogDraft? {
        if (size <= 0) return null
        val text = bytes.copyOfRange(0, size).toString(Charsets.US_ASCII)
        val match = SOURCE_RECORD.matchEntire(text) ?: return null
        val level = when (match.groupValues[1]) {
            "V" -> LogLevel.Verbose
            "D" -> LogLevel.Debug
            "I" -> LogLevel.Info
            "W" -> LogLevel.Warn
            "E" -> LogLevel.Error
            else -> return null
        }
        val sourceElapsedMs = match.groupValues[2].toLongOrNull() ?: return null
        val message = match.groupValues[3]
        if (!isValidRunHostLogMessage(message)) return null
        return LogDraft(origin, level, message, sourceElapsedMs)
    }

    private companion object {
        const val MAX_SOURCE_RECORD_BYTES: Int = RunHostProtocol.MAX_LOG_MESSAGE_BYTES + 64
        const val LF: Byte = 0x0a
        val SOURCE_RECORD: Regex = Regex("\\[([VDIWE])/([0-9]+)ms] (.+)")
    }
}
