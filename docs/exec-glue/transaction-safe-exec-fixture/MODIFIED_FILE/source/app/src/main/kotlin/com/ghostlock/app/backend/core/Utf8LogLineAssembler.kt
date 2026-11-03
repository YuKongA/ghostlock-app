package com.ghostlock.app.backend.core

import java.io.ByteArrayOutputStream

/**
 * Reassembles UTF-8 lines from bounded AIDL log chunks without losing split code points.
 *
 * @param onLine receives each complete non-empty line, in byte-stream order.
 * @return None; lines are emitted incrementally and a terminal suffix is emitted by [finish].
 * @throws IllegalStateException when bytes are appended after the stream has finished.
 * @note The host bounds each source line and the overall stream; this is the App-side exec-log stage.
 */
internal class Utf8LogLineAssembler(private val onLine: (String) -> Unit) {
    private val pending = ByteArrayOutputStream()
    private var finished = false

    /**
     * Appends one byte chunk and emits every complete newline-delimited UTF-8 line.
     *
     * @param bytes next AIDL log bytes; an empty array is a no-op.
     * @return None; incomplete suffix bytes remain owned by this assembler.
     * @throws IllegalStateException if [finish] has already been called.
     */
    fun append(bytes: ByteArray) {
        check(!finished) { "log stream already finished" }
        if (bytes.isEmpty()) return
        val previous = pending.toByteArray()
        val joined = ByteArray(previous.size + bytes.size)
        previous.copyInto(joined)
        bytes.copyInto(joined, destinationOffset = previous.size)
        pending.reset()

        var lineStart = 0
        for (index in joined.indices) {
            if (joined[index] != NEWLINE) continue
            val lineEnd = if (index > lineStart && joined[index - 1] == CARRIAGE_RETURN) index - 1 else index
            if (lineEnd > lineStart) {
                onLine(String(joined, lineStart, lineEnd - lineStart, Charsets.UTF_8))
            }
            lineStart = index + 1
        }
        if (lineStart < joined.size) pending.write(joined, lineStart, joined.size - lineStart)
    }

    /**
     * Emits a final unterminated line and makes repeated finish calls idempotent.
     *
     * @return None; after this call the assembler rejects further input.
     */
    fun finish() {
        if (finished) return
        finished = true
        val tail = pending.toByteArray()
        pending.reset()
        val size = if (tail.lastOrNull() == CARRIAGE_RETURN) tail.size - 1 else tail.size
        if (size > 0) onLine(String(tail, 0, size, Charsets.UTF_8))
    }

    private companion object {
        private const val NEWLINE: Byte = 0x0a
        private const val CARRIAGE_RETURN: Byte = 0x0d
    }
}
