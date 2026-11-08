package com.ghostlock.app.data.settings

import android.content.Context
import android.net.Uri
import android.os.Environment
import android.provider.DocumentsContract
import android.util.Log
import androidx.datastore.core.DataStore
import androidx.datastore.core.DataStoreFactory
import androidx.datastore.core.handlers.ReplaceFileCorruptionHandler
import androidx.datastore.dataStoreFile
import com.ghostlock.app.domain.settings.OperationalPreferences
import com.ghostlock.app.domain.settings.OperationalPreferencesPort
import java.io.File
import java.io.FileOutputStream
import java.nio.file.Files
import java.nio.file.StandardCopyOption
import java.util.UUID
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Deferred
import kotlinx.coroutines.async
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/** Typed operational preferences plus the two Android picker side effects they reference. */
class DataStoreOperationalPreferencesRepository internal constructor(
    private val context: Context,
    dataStore: DataStore<OperationalPreferences>,
    private val scope: CoroutineScope,
) : OperationalPreferencesPort {
    private val mutablePreferences = MutableStateFlow(OperationalPreferences())
    private val commits = DataStoreCommitTracker(dataStore, scope) { value ->
        mutablePreferences.value = value
    }

    override val preferences: StateFlow<OperationalPreferences> = mutablePreferences.asStateFlow()

    /** Submits one actor-current operational transform. */
    override fun submit(
        transform: (OperationalPreferences) -> OperationalPreferences,
    ): Deferred<OperationalPreferences> = commits.submit { current -> normalize(transform(current)) }

    /** Seeds the committed projection before Run and Settings are created. */
    override suspend fun awaitInitial(): OperationalPreferences = commits.awaitInitial()

    /** Queues the exact operational value used by one run cutoff. */
    override fun snapshot(): Deferred<OperationalPreferences> = commits.snapshot()

    /**
     * Copies a font to a same-directory temporary, atomically publishes it, then commits its relative reference.
     *
     * @param uri Serialized document URI selected by Android.
     * @return Accepted operation handle; false means the URI could not be copied safely.
     */
    override fun importTerminalFont(uri: String): Deferred<Boolean> {
        return scope.async {
            if (!copyTerminalFont(uri)) return@async false
            submit { current -> current.copy(terminalFontPath = TERMINAL_FONT_REFERENCE) }.await()
            true
        }
    }

    /** Parses a Downloads tree synchronously, then submits only the normalized relative directory. */
    override fun selectLogExportDirectory(uri: String): Deferred<Boolean> {
        val path = uri.takeIf(String::isNotBlank)
            ?.let { runCatching { Uri.parse(it) }.getOrNull() }
            ?.let(::exportLocationFromTree)
            ?: return completedResult(false)
        val commit = submit { current -> current.copy(logExportLocation = path) }
        return scope.async {
            commit.await()
            true
        }
    }

    /** Copies and fsyncs a selected font before replacing the fixed app-private target. */
    private fun copyTerminalFont(rawUri: String): Boolean {
        if (rawUri.isBlank()) return false
        val uri = runCatching { Uri.parse(rawUri) }.getOrNull() ?: return false
        val directory = File(context.filesDir, "fonts")
        if (!directory.isDirectory && !directory.mkdir()) return false
        val target = File(directory, "terminal-font.ttf")
        val temporary = File(directory, ".terminal-font.${UUID.randomUUID()}.tmp")
        return try {
            val input = context.contentResolver.openInputStream(uri) ?: return false
            input.use { source ->
                FileOutputStream(temporary).use { output ->
                    source.copyTo(output)
                    output.flush()
                    output.fd.sync()
                }
            }
            Files.move(
                temporary.toPath(),
                target.toPath(),
                StandardCopyOption.ATOMIC_MOVE,
                StandardCopyOption.REPLACE_EXISTING,
            )
            target.isFile && target.length() > 0L
        } catch (_: Exception) {
            false
        } finally {
            if (temporary.exists()) temporary.delete()
        }
    }

    /** @return Downloads-relative path represented by a primary-storage tree, or null. */
    private fun exportLocationFromTree(uri: Uri): String? {
        val documentId = runCatching { DocumentsContract.getTreeDocumentId(uri) }.getOrNull() ?: return null
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

    companion object {
        private const val FILE_NAME = "operational-preferences.json"
        private const val TAG = "GhostLockDataStore"
        private const val MAX_BOOT_RETRY_COUNT = 10
        private const val PRIMARY_STORAGE_PREFIX = "primary:"
        const val TERMINAL_FONT_REFERENCE = "fonts/terminal-font.ttf"

        /** Creates the sole operational-preference DataStore. */
        fun create(context: Context, scope: CoroutineScope): DataStoreOperationalPreferencesRepository {
            val applicationContext = context.applicationContext
            val defaultValue = OperationalPreferences()
            return DataStoreOperationalPreferencesRepository(
                context = applicationContext,
                dataStore = DataStoreFactory.create(
                    serializer = KotlinxDataStoreSerializer(
                        serializer = OperationalPreferences.serializer(),
                        defaultValue = defaultValue,
                        validate = ::validate,
                    ),
                    corruptionHandler = ReplaceFileCorruptionHandler { problem ->
                        Log.e(TAG, "operational preferences were corrupt; replacing with defaults", problem)
                        defaultValue
                    },
                    scope = scope,
                    produceFile = { applicationContext.dataStoreFile(FILE_NAME) },
                ),
                scope = scope,
            )
        }

        /** Normalizes bounded values and the MediaStore-relative directory. */
        private fun normalize(value: OperationalPreferences): OperationalPreferences = value.copy(
            bootRetryCount = value.bootRetryCount.coerceIn(0, MAX_BOOT_RETRY_COUNT),
            logExportLocation = normalizeExportLocation(value.logExportLocation),
            terminalFontPath = value.terminalFontPath?.takeIf { it == TERMINAL_FONT_REFERENCE },
        )

        /** Rejects malformed bytes instead of silently changing persisted operational semantics. */
        private fun validate(value: OperationalPreferences) {
            require(value.bootRetryCount in 0..MAX_BOOT_RETRY_COUNT) { "boot retry count" }
            require(value.logExportLocation == normalizeExportLocation(value.logExportLocation)) {
                "log export location"
            }
            require(value.terminalFontPath == null || value.terminalFontPath == TERMINAL_FONT_REFERENCE) {
                "terminal font reference"
            }
        }

        /** @return Canonical Downloads-relative path with no traversal segment. */
        private fun normalizeExportLocation(value: String?): String {
            val cleaned = value.orEmpty().trim().trim('/').replace(Regex("/{2,}"), "/")
            val safe = cleaned.takeIf { it.isNotEmpty() && hasSafeSegments(it) }
                ?: OperationalPreferences.DEFAULT_LOG_EXPORT_LOCATION
            val downloads = Environment.DIRECTORY_DOWNLOADS
            return if (safe == downloads || safe.startsWith(downloads + "/")) safe else "$downloads/$safe"
        }

        /** @return True when every path segment is ordinary and non-empty. */
        private fun hasSafeSegments(value: String): Boolean =
            value.split('/').all { it.isNotEmpty() && it != "." && it != ".." }

        /** Returns an already-completed non-throwing picker result. */
        private fun completedResult(value: Boolean): Deferred<Boolean> =
            kotlinx.coroutines.CompletableDeferred(value)
    }
}
