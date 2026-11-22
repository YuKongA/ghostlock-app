package com.ghostlock.plugin.diagnose

import android.os.ParcelFileDescriptor
import com.ghostlock.plugin.api.v1.DiagnosticRunContext
import com.ghostlock.plugin.api.v1.PluginLogLevel
import com.ghostlock.plugin.api.v1.PluginLogOrigin

/** Drains and validates one exact native source record without depending on Host implementation classes. */
internal object PluginPipeLogCollector {
    fun collect(
        descriptor: ParcelFileDescriptor,
        context: DiagnosticRunContext,
        origin: PluginLogOrigin,
        expectedBody: String,
    ): Boolean = runCatching {
        val line = buildString {
            ParcelFileDescriptor.AutoCloseInputStream(descriptor).use { input ->
                val buffer = ByteArray(STREAM_BUFFER_BYTES)
                var sawLineFeed = false
                while (true) {
                    val count = input.read(buffer)
                    if (count < 0) break
                    repeat(count) { offset ->
                        val value = buffer[offset].toInt() and 0xff
                        require(!sawLineFeed) { "extra Diagnose log record" }
                        if (value == '\n'.code) {
                            sawLineFeed = true
                        } else {
                            require(value in 0x20..0x7e) { "non-ASCII Diagnose log byte" }
                            append(value.toChar())
                        }
                    }
                }
                require(sawLineFeed) { "partial Diagnose log record" }
            }
        }
        val match = SOURCE_RECORD.matchEntire(line) ?: error("malformed Diagnose log record")
        require(match.groupValues[1] == "I") { "unexpected Diagnose log severity" }
        require(match.groupValues[2].toLongOrNull() != null) { "invalid Diagnose elapsed time" }
        require(match.groupValues[3] == expectedBody) { "unexpected Diagnose log body" }
        runCatching { context.emit(origin, PluginLogLevel.INFO, expectedBody) }
        true
    }.getOrDefault(false)

    private const val STREAM_BUFFER_BYTES = 4096
    private val SOURCE_RECORD = Regex("\\[([VDIWE])/([0-9]+)ms] (.+)")
}
