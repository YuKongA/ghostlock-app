package com.ghostlock.app.log

import android.content.ContentValues
import android.content.Context
import android.provider.MediaStore
import com.ghostlock.app.domain.settings.OperationalPreferences
import java.io.BufferedWriter
import java.io.OutputStreamWriter
import java.nio.charset.StandardCharsets
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.concurrent.atomic.AtomicLong

/**
 * 自动日志导出：把一次执行的日志写到 Downloads/ghostlock-log/<时间戳>/。
 *
 * 与 remote/main 的 DebugAttackLog 同一套 MediaStore 方案：无需存储权限，
 * 文件在下载目录里直接可见；每行 flush，进程被中止也能留下已产生的日志。
 */
object LogExport {
    /** 下载目录下的固定根目录。 */
    const val BASE_FOLDER = "Download/ghostlock-log"

    private val sequence = AtomicLong()

    class Session internal constructor(
        private val writer: BufferedWriter,
        val displayName: String,
        val folderPath: String,
    ) {
        @Synchronized
        fun append(line: String) {
            runCatching {
                writer.write(line)
                writer.newLine()
                writer.flush()
            }
        }

        @Synchronized
        fun close() {
            runCatching { writer.close() }
        }
    }

    /**
     * Opens one MediaStore log session when automatic export is enabled.
     *
     * @param context Android context providing the Downloads resolver.
     * @param entry Stable run entry label used in the display name.
     * @param preferences Immutable operational preference snapshot supplied by the caller.
     * @return Writable session, or null when export is disabled or MediaStore cannot open it.
     */
    fun open(
        context: Context,
        entry: String,
        preferences: OperationalPreferences,
    ): Session? {
        if (!preferences.autoExportLogs) return null
        val stamp = SimpleDateFormat("yyyyMMdd-HHmmss", Locale.US).format(Date())
        val folderPath = preferences.logExportLocation + "/" + stamp
        val displayName = "ghostlock-$entry-${sequence.getAndIncrement()}.log"
        val values = ContentValues().apply {
            put(MediaStore.MediaColumns.DISPLAY_NAME, displayName)
            put(MediaStore.MediaColumns.MIME_TYPE, "text/plain")
            put(MediaStore.MediaColumns.RELATIVE_PATH, folderPath)
        }
        val resolver = context.contentResolver
        val uri = runCatching { resolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, values) }
            .getOrNull() ?: return null
        return runCatching {
            val output = resolver.openOutputStream(uri, "w") ?: error("cannot open export")
            Session(BufferedWriter(OutputStreamWriter(output, StandardCharsets.UTF_8)), displayName, folderPath)
        }.getOrNull()
    }
}
