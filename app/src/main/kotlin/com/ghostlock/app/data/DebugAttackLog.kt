package com.ghostlock.app.data

import android.content.ContentValues
import android.content.Context
import android.os.Environment
import android.provider.MediaStore
import com.ghostlock.app.BuildConfig
import java.io.BufferedWriter
import java.io.Closeable
import java.io.OutputStreamWriter
import java.nio.charset.StandardCharsets
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.concurrent.atomic.AtomicLong

/** Debug-only, immediately visible archive for one attack attempt. */
internal class DebugAttackLog private constructor(
    val displayName: String,
    private val writer: BufferedWriter,
) : Closeable {
    @Synchronized
    fun append(line: String) {
        writer.write(line)
        writer.newLine()
        writer.flush()
    }

    @Synchronized
    override fun close() {
        writer.close()
    }

    companion object {
        private val sequence = AtomicLong()

        fun open(context: Context, entry: String): DebugAttackLog? {
            if (!BuildConfig.DEBUG) return null
            val timestamp = SimpleDateFormat("yyyyMMdd-HHmmss-SSS", Locale.US).format(Date())
            val suffix = sequence.getAndIncrement()
            val displayName = "ghostlock-$timestamp-$suffix-$entry.log"
            val values = ContentValues().apply {
                put(MediaStore.MediaColumns.DISPLAY_NAME, displayName)
                put(MediaStore.MediaColumns.MIME_TYPE, "text/plain")
                put(
                    MediaStore.MediaColumns.RELATIVE_PATH,
                    "${Environment.DIRECTORY_DOWNLOADS}/GhostLock",
                )
            }
            val resolver = context.contentResolver
            val uri = resolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, values)
                ?: return null
            return try {
                val output = resolver.openOutputStream(uri, "w")
                    ?: error("cannot open debug log output")
                DebugAttackLog(
                    displayName,
                    BufferedWriter(OutputStreamWriter(output, StandardCharsets.UTF_8)),
                )
            } catch (_: Exception) {
                resolver.delete(uri, null, null)
                null
            }
        }
    }
}
