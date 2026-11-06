package com.ghostlock.app.backend.transport

import com.ghostlock.app.domain.run.RunLogDraft
import com.ghostlock.app.domain.run.RunLogLevel
import com.ghostlock.app.domain.run.RunLogOrigin
import com.ghostlock.app.run.RunHostLogRecord
import java.util.concurrent.atomic.AtomicLong

/**
 * Validates one producer log draft before RunHost sequencing.
 *
 * @return True only for a bounded single-line ASCII message using the approved SGR subset.
 */
internal fun RunLogDraft.isValidRunHostLogDraft(): Boolean = isValidRunLogMessage(message)

/**
 * Validates printable ASCII plus the exact foreground/reset SGR grammar.
 *
 * @param message Producer message without a rendered origin/level prefix.
 * @return True when every byte is printable ASCII or part of one approved SGR sequence.
 */
internal fun isValidRunLogMessage(message: String): Boolean {
    if (message.toByteArray(Charsets.US_ASCII).size > RunHostProtocol.MAX_LOG_MESSAGE_BYTES) return false
    var index = 0
    while (index < message.length) {
        val code = message[index].code
        if (code in PRINTABLE_ASCII_RANGE) {
            index += 1
            continue
        }
        if (code != ESCAPE_CODE) return false
        val next = approvedSgrEnd(message, index) ?: return false
        index = next
    }
    return true
}

/**
 * Reserves the next bounded log sequence without allowing callback backlog to grow unbounded.
 *
 * @param counter Per-run published-log sequence.
 * @return The next sequence, or null once the fixed log budget is exhausted.
 */
internal fun reserveRunHostLogSequence(counter: AtomicLong): Long? {
    while (true) {
        val current = counter.get()
        if (current < 0L || current >= RunHostProtocol.MAX_QUEUED_LOGS.toLong()) return null
        val next = current + 1L
        if (counter.compareAndSet(current, next)) return next
    }
}

/**
 * Per-run client authority for accepting ordered log callbacks from one typed RunHost.
 *
 * @param identity Immutable run identity established before callback registration.
 * @note Source sequence rejects duplicates and inversions; RunCoordinator commit order remains global authority.
 */
internal class RunHostLogCursor(
    private val identity: RunHostIdentity,
) {
    private val lock = Any()
    private var lastSequence: Long = 0L

    /**
     * Accepts only a bounded current record with a strictly increasing source sequence.
     *
     * @param record Generated typed wire record.
     * @return True when the record is current, structurally valid, and newly ordered.
     */
    fun accept(record: RunHostLogRecord): Boolean {
        if (runCatching { record.parcelDataSize() }.getOrDefault(Int.MAX_VALUE) >
            RunHostProtocol.MAX_LOG_PARCEL_BYTES
        ) {
            return false
        }
        if (record.runId != identity.runId || record.generation != identity.generation) return false
        if (record.sequence !in 1L..RunHostProtocol.MAX_QUEUED_LOGS.toLong()) return false
        if (record.elapsedRealtimeMs < 0L) return false
        if (RunLogOrigin.fromWireValue(record.origin) == null) return false
        if (RunLogLevel.fromWireValue(record.level) == null) return false
        if (!isValidRunLogMessage(record.message)) return false
        return synchronized(lock) {
            if (record.sequence <= lastSequence) {
                false
            } else {
                lastSequence = record.sequence
                true
            }
        }
    }
}

/** @return Index immediately after a valid SGR sequence, or null. */
private fun approvedSgrEnd(message: String, start: Int): Int? {
    if (start + 3 >= message.length || message[start + 1] != '[') return null
    val limit = minOf(message.length, start + MAX_SGR_BYTES)
    var end = start + 2
    while (end < limit && message[end] != 'm') end += 1
    if (end >= limit || message[end] != 'm') return null
    val parameters = message.substring(start + 2, end).split(';')
    if (parameters.size !in 1..MAX_SGR_PARAMETERS) return null
    val valid = parameters.all { parameter ->
        parameter.length in 1..MAX_SGR_PARAMETER_DIGITS &&
            parameter.all(Char::isDigit) &&
            parameter.toIntOrNull() in APPROVED_SGR_PARAMETERS
    }
    return if (valid) end + 1 else null
}

private val PRINTABLE_ASCII_RANGE: IntRange = 0x20..0x7e
private val APPROVED_SGR_PARAMETERS: Set<Int> = setOf(0, 39) + (30..37) + (90..97)
private const val ESCAPE_CODE: Int = 0x1b
private const val MAX_SGR_BYTES: Int = 16
private const val MAX_SGR_PARAMETERS: Int = 4
private const val MAX_SGR_PARAMETER_DIGITS: Int = 3
