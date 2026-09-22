package com.ghostlock.app.data

import android.content.ContentValues
import android.content.Context
import android.media.MediaScannerConnection
import android.os.Environment
import android.provider.MediaStore
import java.io.BufferedWriter
import java.io.Closeable
import java.io.File
import java.io.OutputStreamWriter
import java.nio.charset.StandardCharsets
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.concurrent.atomic.AtomicLong

/** Debug-only, immediately visible archive for one attack attempt. */
internal class DebugAttackLog private constructor(
    private val context: Context,
    val displayName: String,
    /** Downloads-relative folder that holds the attempt log and its dumps. */
    val folderPath: String,
    private val writer: BufferedWriter,
) : Closeable {
    /**
     * Absolute folder handed to the native process through --dump-kernel-log;
     * the root prepare script drops kernel/pstore/iomem dumps next to the log.
     */
    @Suppress("DEPRECATION")
    val folderFile: File
        get() = File(Environment.getExternalStorageDirectory(), folderPath)

    @Synchronized
    fun append(line: String) {
        writer.write(line)
        writer.newLine()
        writer.flush()
    }

    @Synchronized
    override fun close() {
        writer.close()
        publishSidecars()
    }

    /** Ask MediaStore to index the root-written dumps so file managers show them. */
    private fun publishSidecars() {
        val paths = buildList {
            for (name in SidecarNames) {
                val file = File(folderFile, name)
                if (file.isFile) add(file.absolutePath)
            }
            val pstore = folderFile.resolve("pstore")
            if (pstore.isDirectory) {
                pstore.listFiles()?.forEach { if (it.isFile) add(it.absolutePath) }
            }
        }
        if (paths.isEmpty()) return
        runCatching {
            MediaScannerConnection.scanFile(context, paths.toTypedArray(), null, null)
        }
    }

    companion object {
        private val SidecarNames = listOf(
            "kernel-dmesg.log", "kernel-info.txt", "iomem.txt",
            "ksu.log",
        )
        private val sequence = AtomicLong()

        fun open(context: Context, entry: String, folder: String): DebugAttackLog? {
            val stamp = SimpleDateFormat("yyyyMMdd-HHmmss", Locale.US).format(Date())
            val suffix = sequence.getAndIncrement()
            val folderPath = "$folder/$stamp"
            val displayName = "ghostlock-$entry-$suffix.log"
            val values = ContentValues().apply {
                put(MediaStore.MediaColumns.DISPLAY_NAME, displayName)
                put(MediaStore.MediaColumns.MIME_TYPE, "text/plain")
                put(MediaStore.MediaColumns.RELATIVE_PATH, folderPath)
            }
            val resolver = context.contentResolver
            val uri = resolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, values)
                ?: return null
            return try {
                val output = resolver.openOutputStream(uri, "w")
                    ?: error("cannot open debug log output")
                DebugAttackLog(
                    context.applicationContext,
                    displayName,
                    folderPath,
                    BufferedWriter(OutputStreamWriter(output, StandardCharsets.UTF_8)),
                )
            } catch (_: Exception) {
                resolver.delete(uri, null, null)
                null
            }
        }
    }
}
