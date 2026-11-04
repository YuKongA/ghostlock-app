package com.ghostlock.app.backend.backends.lua

import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/**
 * Process-local Lua script selection exposed to presentation as immutable state.
 *
 * @property uri Persistable document URI, or null when no readable script is selected.
 * @property summary Bounded display summary derived from the selected document.
 */
data class LuaScriptSelection(
    val uri: Uri? = null,
    val summary: String? = null,
)

/**
 * Owns the Lua script selection independently from Compose and renderer lifetime.
 *
 * Persistence remains delegated through [BackendCustom]'s standard configuration capability; this
 * owner validates restored URI access and exposes a read-only stream to the outer adapter.
 */
object LuaScriptSelectionStore {
    private val mutableState = MutableStateFlow(LuaScriptSelection())

    /** Current selection stream; renderers cannot mutate it directly. */
    val state: StateFlow<LuaScriptSelection> = mutableState.asStateFlow()

    /**
     * Selects or clears one script and derives its bounded summary.
     *
     * @param context Android content resolver owner.
     * @param uri New document URI, or null to clear the selection.
     */
    fun select(context: Context, uri: Uri?) {
        mutableState.value = LuaScriptSelection(
            uri = uri,
            summary = uri?.let { selected ->
                runCatching { LuaScriptHost.readSummary(context, selected) }.getOrNull()
            },
        )
    }

    /**
     * Publishes a document whose content was already validated before the canonical store commit.
     *
     * @param uri Persisted readable URI.
     * @param summary Bounded summary derived before persistence.
     * @note This avoids changing runtime state when the typed store commit fails.
     */
    internal fun selectValidated(uri: Uri, summary: String) {
        mutableState.value = LuaScriptSelection(uri = uri, summary = summary)
    }

    /**
     * Restores a persisted URI only when the provider remains readable.
     *
     * @param context Android content resolver owner.
     * @param raw Persisted URI string; blank clears the selection.
     * @return True only when a readable document was restored.
     */
    fun restore(context: Context, raw: String): Boolean {
        val parsed = raw.takeIf(String::isNotBlank)?.let { value ->
            runCatching { Uri.parse(value) }.getOrNull()
        }
        if (parsed == null) {
            select(context, null)
            return false
        }
        val readable = runCatching {
            context.contentResolver.openInputStream(parsed)?.use { true } ?: false
        }.getOrDefault(false)
        if (!readable) {
            select(context, null)
            return false
        }
        select(context, parsed)
        return true
    }
}

/**
 * Bounded Lua document reader and future runtime seam.
 *
 * @note No script execution is wired; the current name intentionally reports that state.
 */
object LuaScriptHost {
    /** User-visible runtime fact consumed by both UI skins. */
    const val runtimeName: String = "未接线（默认不执行）"

    /**
     * Reads at most [maxBytes] and returns a filename, byte count, and first non-blank line.
     *
     * @param context Android content resolver owner.
     * @param uri Selected document URI.
     * @param maxBytes Maximum bytes read from the provider.
     * @return Bounded display summary.
     */
    fun readSummary(context: Context, uri: Uri, maxBytes: Int = 4096): String {
        val name = context.contentResolver.query(uri, null, null, null, null)?.use { cursor ->
            val index = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME)
            if (index >= 0 && cursor.moveToFirst()) cursor.getString(index) else null
        } ?: uri.lastPathSegment ?: "script.lua"
        val text = runCatching {
            context.contentResolver.openInputStream(uri)?.use { input ->
                val buffer = ByteArray(maxBytes)
                val read = input.read(buffer)
                String(buffer, 0, if (read > 0) read else 0)
            }
        }.getOrNull().orEmpty()
        val firstLine = text.lineSequence().firstOrNull(String::isNotBlank) ?: "(空文件)"
        return name + " · " + text.length + " 字节\n" + firstLine.take(120)
    }
}
