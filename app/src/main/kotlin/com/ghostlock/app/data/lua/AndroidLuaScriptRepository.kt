package com.ghostlock.app.data.lua

import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/** Immutable selected Lua frontend document facts. */
data class LuaScriptDocument(
    val uri: String,
    val displayName: String,
    val byteCount: Long,
    val summary: String,
)

/**
 * Android document adapter for the optional Lua frontend.
 *
 * Selection is process-local until a profile-v3 editor port records the document reference. Script bytes are
 * never put in SharedPreferences, BackendSpec, Bundle, or the RunHost Binder request.
 */
class AndroidLuaScriptRepository(context: Context) {
    private val applicationContext = context.applicationContext
    private val mutableSelection = MutableStateFlow<LuaScriptDocument?>(null)

    /** Current validated document selection. */
    val selection: StateFlow<LuaScriptDocument?> = mutableSelection.asStateFlow()

    /**
     * Reads bounded metadata/summary and publishes one validated selection.
     *
     * @param rawUri Persistable document URI string.
     * @return True when the provider yielded a readable regular document stream.
     */
    fun select(rawUri: String): Boolean {
        val uri = runCatching { Uri.parse(rawUri) }.getOrNull()
            ?.takeIf { parsed -> !parsed.scheme.isNullOrBlank() }
            ?: return false
        val document = runCatching { inspect(uri) }.getOrNull() ?: return false
        mutableSelection.value = document
        return true
    }

    /** Clears process-local selection without mutating an external profile document. */
    fun clear() {
        mutableSelection.value = null
    }

    /** Reads provider metadata and at most the bounded preview size. */
    private fun inspect(uri: Uri): LuaScriptDocument {
        var displayName = uri.lastPathSegment ?: "script.lua"
        var declaredSize = -1L
        applicationContext.contentResolver.query(uri, null, null, null, null)?.use { cursor ->
            val nameIndex = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME)
            val sizeIndex = cursor.getColumnIndex(OpenableColumns.SIZE)
            if (cursor.moveToFirst()) {
                if (nameIndex >= 0) displayName = cursor.getString(nameIndex) ?: displayName
                if (sizeIndex >= 0 && !cursor.isNull(sizeIndex)) declaredSize = cursor.getLong(sizeIndex)
            }
        }
        val preview = applicationContext.contentResolver.openInputStream(uri)?.use { input ->
            val buffer = ByteArray(MAX_PREVIEW_BYTES)
            val count = input.read(buffer).coerceAtLeast(0)
            String(buffer, 0, count, Charsets.UTF_8)
        } ?: error("Lua document is unreadable")
        return LuaScriptDocument(
            uri = uri.toString(),
            displayName = displayName,
            byteCount = declaredSize,
            summary = preview.lineSequence().firstOrNull(String::isNotBlank)?.take(MAX_SUMMARY_CHARS)
                ?: "(empty document)",
        )
    }

    private companion object {
        const val MAX_PREVIEW_BYTES: Int = 4 * 1024
        const val MAX_SUMMARY_CHARS: Int = 120
    }
}
