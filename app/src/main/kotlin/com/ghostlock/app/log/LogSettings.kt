package com.ghostlock.app.log

import android.content.Context
import android.net.Uri
import android.os.Environment
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.text.font.Font
import androidx.compose.ui.text.font.FontFamily
import java.io.File

/**
 * 终端日志的显示设置（等级 / 字体 / 时间戳）。
 *
 * 持久化在 `ghostlock.log`；等级同时应用到 [log.configure]，因此终端与文件使用同一阈值。
 */
object LogSettings {
    private const val PREFS = "ghostlock.log"
    private const val KEY_LEVEL = "level"
    private const val KEY_FONT = "font_path"
    private const val KEY_TIMESTAMP = "show_timestamp"
    private const val KEY_AUTO_EXPORT = "auto_export"
    private const val KEY_EXPORT_LOCATION = "export_location"

    const val DEFAULT_EXPORT_LOCATION = "Download/ghostlock-log"

    var level by mutableStateOf(LogLevel.Info)
        private set

    /** 自定义字体路径；null = 系统等宽。 */
    var fontPath by mutableStateOf<String?>(null)
        private set

    var showTimestamp by mutableStateOf(false)
        private set

    /** 执行结束后自动把日志导出到 Downloads/ghostlock-log（默认开启）。 */
    var autoExport by mutableStateOf(true)
        private set

    /** 自动导出根目录（外部存储相对路径），默认 Download/ghostlock-log。 */
    var exportLocation by mutableStateOf(DEFAULT_EXPORT_LOCATION)
        private set

    /** 进程启动时调用一次：读设置并立即应用阈值。 */
    fun attach(context: Context) {
        val prefs = context.getSharedPreferences(PREFS, 0)
        level = runCatching { LogLevel.valueOf(prefs.getString(KEY_LEVEL, LogLevel.Info.name)!!) }
            .getOrDefault(LogLevel.Info)
        fontPath = prefs.getString(KEY_FONT, null)?.takeIf { File(it).isFile }
        showTimestamp = prefs.getBoolean(KEY_TIMESTAMP, false)
        autoExport = prefs.getBoolean(KEY_AUTO_EXPORT, true)
        exportLocation = normalizeExportLocation(prefs.getString(KEY_EXPORT_LOCATION, null))
        log.configure(level)
    }

    fun setLevel(context: Context, value: LogLevel) {
        level = value
        log.configure(value)
        context.getSharedPreferences(PREFS, 0).edit().putString(KEY_LEVEL, value.name).apply()
    }

    fun setShowTimestamp(context: Context, value: Boolean) {
        showTimestamp = value
        context.getSharedPreferences(PREFS, 0).edit().putBoolean(KEY_TIMESTAMP, value).apply()
    }

    fun setAutoExport(context: Context, value: Boolean) {
        autoExport = value
        context.getSharedPreferences(PREFS, 0).edit().putBoolean(KEY_AUTO_EXPORT, value).apply()
    }

    fun setExportLocation(context: Context, value: String) {
        exportLocation = normalizeExportLocation(value)
        context.getSharedPreferences(PREFS, 0).edit().putString(KEY_EXPORT_LOCATION, exportLocation).apply()
    }

    /**
     * 把 SAF 选择的目录 URI 转成 MediaStore（Downloads 集合）能用的相对路径。
     *
     * 只接受主存储的 Download 子树：树 URI 的 document id 形如
     * primary:Download/ghostlock-log，而 MediaStore.Downloads 的 RELATIVE_PATH
     * 必须带 Download/ 前缀；其它目录（Documents 等）Downloads 集合表达不了，
     * 直接返回 null 让 UI 提示无效（2026-10-19 修复：之前只返回 "ghostlock-log"，
     * 插入时被系统拒绝，导出静默失败）。
     *
     * @param uri ACTION_OPEN_DOCUMENT_TREE 返回的树 URI。
     * @return Download/... 相对路径；不可用时 null。
     */
    fun exportLocationFromTree(uri: Uri): String? {
        val id = runCatching { android.provider.DocumentsContract.getTreeDocumentId(uri) }.getOrNull() ?: return null
        if (!id.startsWith("primary:")) return null
        val relative = id.removePrefix("primary:").trim('/')
        val download = Environment.DIRECTORY_DOWNLOADS
        val path = when {
            relative == download -> download
            relative.startsWith(download + "/") -> relative
            else -> null
        }
        return path?.takeIf { candidate -> candidate.split('/').none { it == ".." || it == "." } }
    }

    /** @return 规范化后的导出根目录；始终位于 Download 子树下（MediaStore.Downloads 的要求）。 */
    fun normalizeExportLocation(value: String?): String {
        val cleaned = value.orEmpty().trim().trim('/').replace(Regex("/{2,}"), "/")
        val safe = cleaned.takeIf { candidate ->
            candidate.isNotEmpty() && candidate.split('/').none { it == ".." || it == "." }
        } ?: DEFAULT_EXPORT_LOCATION
        val download = Environment.DIRECTORY_DOWNLOADS
        return if (safe == download || safe.startsWith(download + "/")) safe else download + "/" + safe
    }

    /** 选中的字体文件复制到私有目录后调用。 */
    fun setFont(context: Context, uri: Uri): String? {
        val dir = File(context.filesDir, "fonts").apply { mkdirs() }
        val dest = File(dir, "terminal-font.ttf")
        val copied = runCatching {
            context.contentResolver.openInputStream(uri)?.use { input ->
                dest.outputStream().use { output -> input.copyTo(output) }
            }
            if (dest.isFile) dest.absolutePath else null
        }.getOrNull()
        if (copied != null) {
            fontPath = copied
            context.getSharedPreferences(PREFS, 0).edit().putString(KEY_FONT, copied).apply()
        }
        return copied
    }

    fun clearFont(context: Context) {
        fontPath = null
        context.getSharedPreferences(PREFS, 0).edit().remove(KEY_FONT).apply()
    }

    /** 终端 Text 用的字体族：默认系统等宽，自定义时用打包/复制进来的 ttf。 */
    fun fontFamily(): FontFamily {
        val path = fontPath ?: return FontFamily.Monospace
        val file = File(path)
        return if (file.isFile) FontFamily(Font(file)) else FontFamily.Monospace
    }
}
