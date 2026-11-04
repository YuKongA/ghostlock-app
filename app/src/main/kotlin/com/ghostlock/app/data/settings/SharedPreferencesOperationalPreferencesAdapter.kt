package com.ghostlock.app.data.settings

import android.content.Context
import android.content.SharedPreferences
import android.net.Uri
import android.os.Environment
import android.provider.DocumentsContract
import com.ghostlock.app.domain.settings.OperationalPreferences
import com.ghostlock.app.domain.settings.OperationalPreferencesPort
import com.ghostlock.app.domain.settings.TerminalLogLevel
import com.ghostlock.app.log.LogLevel
import com.ghostlock.app.log.log
import java.io.File
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/**
 * Application-scoped SharedPreferences adapter for operational settings and Android picker effects.
 *
 * @param context Application-capable context used for the historical stores and private font copy.
 * @note This adapter is the single mutable authority; it preserves all historical store names, keys,
 *   defaults, export normalization, and immediate log-threshold application without singleton state.
 */
class SharedPreferencesOperationalPreferencesAdapter(
    context: Context,
) : OperationalPreferencesPort {
    private val applicationContext = context.applicationContext
    private val logStore = applicationContext.getSharedPreferences(
        LOG_PREFERENCES,
        Context.MODE_PRIVATE,
    )
    private val attackStore = applicationContext.getSharedPreferences(
        ATTACK_PREFERENCES,
        Context.MODE_PRIVATE,
    )
    private val bootStore = applicationContext.getSharedPreferences(
        BOOT_PREFERENCES,
        Context.MODE_PRIVATE,
    )
    private val stores = listOf(logStore, attackStore, bootStore)
    private val mutablePreferences = MutableStateFlow(readCurrent())
    private val listener = SharedPreferences.OnSharedPreferenceChangeListener { _, _ -> publish() }

    override val preferences: StateFlow<OperationalPreferences> = mutablePreferences.asStateFlow()

    init {
        applyLogThreshold(mutablePreferences.value.logLevel)
        stores.forEach { store -> store.registerOnSharedPreferenceChangeListener(listener) }
    }

    /**
     * Persists one complete normalized snapshot to the historical stores.
     *
     * @param value Storage-neutral state; retry count and export path are normalized before writing.
     */
    override fun save(value: OperationalPreferences) {
        val normalized = value.copy(
            bootRetryCount = value.bootRetryCount.coerceIn(0, MAX_BOOT_RETRY_COUNT),
            logExportLocation = normalizeExportLocation(value.logExportLocation),
        )
        val current = readCurrent()
        if (normalized == current) return

        logStore.edit()
            .putString(KEY_LOG_LEVEL, normalized.logLevel.name)
            .putBoolean(KEY_LOG_TIMESTAMP, normalized.showLogTimestamp)
            .putBoolean(KEY_LOG_AUTO_EXPORT, normalized.autoExportLogs)
            .putString(KEY_LOG_EXPORT_LOCATION, normalized.logExportLocation)
            .apply()
        attackStore.edit()
            .putBoolean(KEY_WARN_ON_RERUN, normalized.warnOnRerun)
            .apply()
        bootStore.edit()
            .putBoolean(KEY_AUTO_ATTACK, normalized.autoAttackOnBoot)
            .putInt(KEY_BOOT_RETRY_COUNT, normalized.bootRetryCount)
            .apply()
        applyLogThreshold(normalized.logLevel)
        publish()
    }

    /**
     * Copies the selected font into application-private storage and persists its path.
     *
     * @param uri Serialized document URI.
     * @return True only when a readable private font file was created and selected.
     */
    override fun importTerminalFont(uri: String): Boolean {
        if (uri.isBlank()) return false
        val parsed = runCatching { Uri.parse(uri) }.getOrNull() ?: return false
        val directory = File(applicationContext.filesDir, "fonts").apply { mkdirs() }
        val destination = File(directory, "terminal-font.ttf")
        val copied = runCatching {
            applicationContext.contentResolver.openInputStream(parsed)?.use { input ->
                destination.outputStream().use { output -> input.copyTo(output) }
            }
            destination.takeIf(File::isFile)?.absolutePath
        }.getOrNull() ?: return false
        logStore.edit().putString(KEY_LOG_FONT, copied).apply()
        publish()
        return true
    }

    /**
     * Selects a primary-storage Downloads tree for log export.
     *
     * @param uri Serialized tree URI.
     * @return True when the directory can be represented by MediaStore Downloads.
     */
    override fun selectLogExportDirectory(uri: String): Boolean {
        if (uri.isBlank()) return false
        val parsed = runCatching { Uri.parse(uri) }.getOrNull() ?: return false
        val path = exportLocationFromTree(parsed) ?: return false
        logStore.edit().putString(KEY_LOG_EXPORT_LOCATION, path).apply()
        publish()
        return true
    }

    /** Publishes current stores and immediately applies the logging threshold. */
    private fun publish() {
        val current = readCurrent()
        applyLogThreshold(current.logLevel)
        mutablePreferences.value = current
    }

    /** @return Pure snapshot read from the three historical stores. */
    private fun readCurrent(): OperationalPreferences {
        val level = runCatching {
            TerminalLogLevel.valueOf(
                logStore.getString(KEY_LOG_LEVEL, TerminalLogLevel.Info.name)
                    ?: TerminalLogLevel.Info.name,
            )
        }.getOrDefault(TerminalLogLevel.Info)
        return OperationalPreferences(
            logLevel = level,
            terminalFontPath = logStore.getString(KEY_LOG_FONT, null)
                ?.takeIf { path -> File(path).isFile },
            showLogTimestamp = logStore.getBoolean(KEY_LOG_TIMESTAMP, false),
            autoExportLogs = logStore.getBoolean(KEY_LOG_AUTO_EXPORT, true),
            logExportLocation = normalizeExportLocation(
                logStore.getString(KEY_LOG_EXPORT_LOCATION, null),
            ),
            warnOnRerun = attackStore.getBoolean(KEY_WARN_ON_RERUN, true),
            autoAttackOnBoot = bootStore.getBoolean(KEY_AUTO_ATTACK, false),
            bootRetryCount = bootStore.getInt(KEY_BOOT_RETRY_COUNT, 1)
                .coerceIn(0, MAX_BOOT_RETRY_COUNT),
        )
    }

    /** Applies a pure-domain severity to the existing logging sink. */
    private fun applyLogThreshold(level: TerminalLogLevel) {
        log.configure(
            when (level) {
                TerminalLogLevel.Verbose -> LogLevel.Verbose
                TerminalLogLevel.Debug -> LogLevel.Debug
                TerminalLogLevel.Info -> LogLevel.Info
                TerminalLogLevel.Warn -> LogLevel.Warn
                TerminalLogLevel.Error -> LogLevel.Error
            },
        )
    }

    /** @return Downloads-relative path represented by a primary-storage tree, or null. */
    private fun exportLocationFromTree(uri: Uri): String? {
        val documentId = runCatching { DocumentsContract.getTreeDocumentId(uri) }
            .getOrNull() ?: return null
        if (!documentId.startsWith(PRIMARY_STORAGE_PREFIX)) return null
        val relative = documentId.removePrefix(PRIMARY_STORAGE_PREFIX).trim('/')
        val downloads = Environment.DIRECTORY_DOWNLOADS
        val path = when {
            relative == downloads -> downloads
            relative.startsWith(downloads + "/") -> relative
            else -> null
        }
        return path?.takeIf(::hasSafeSegments)
    }

    /** @return Normalized Downloads path with no dot traversal segment. */
    private fun normalizeExportLocation(value: String?): String {
        val cleaned = value.orEmpty().trim().trim('/').replace(Regex("/{2,}"), "/")
        val safe = cleaned.takeIf { candidate ->
            candidate.isNotEmpty() && hasSafeSegments(candidate)
        } ?: OperationalPreferences.DEFAULT_LOG_EXPORT_LOCATION
        val downloads = Environment.DIRECTORY_DOWNLOADS
        return if (safe == downloads || safe.startsWith(downloads + "/")) {
            safe
        } else {
            downloads + "/" + safe
        }
    }

    /** @return True when no path segment is dot or dot-dot. */
    private fun hasSafeSegments(value: String): Boolean =
        value.split('/').none { segment -> segment == "." || segment == ".." }

    private companion object {
        const val LOG_PREFERENCES = "ghostlock.log"
        const val ATTACK_PREFERENCES = "ghostlock.attack"
        const val BOOT_PREFERENCES = "ghostlock.boot"
        const val KEY_LOG_LEVEL = "level"
        const val KEY_LOG_FONT = "font_path"
        const val KEY_LOG_TIMESTAMP = "show_timestamp"
        const val KEY_LOG_AUTO_EXPORT = "auto_export"
        const val KEY_LOG_EXPORT_LOCATION = "export_location"
        const val KEY_WARN_ON_RERUN = "warn_on_rerun"
        const val KEY_AUTO_ATTACK = "auto_attack"
        const val KEY_BOOT_RETRY_COUNT = "boot_retry_count"
        const val MAX_BOOT_RETRY_COUNT = 10
        const val PRIMARY_STORAGE_PREFIX = "primary:"
    }
}
