package com.ghostlock.app.domain.repository

import com.ghostlock.app.domain.model.CpuPair
import com.ghostlock.app.domain.model.ProfileConfig

/**
 * Single authority for profile configuration: loads the built-in, imported and
 * overridden JSON, owns the resolved in-memory model, persists sparse overrides
 * and produces the document handed to the native process.
 */
interface ProfileConfigController {
    /** Resolves builtin + imported + overrides into the controller model. */
    suspend fun load(release: String, pair: CpuPair): ProfileConfig

    /** Persists general (execution tuning) edits and returns the new config. */
    suspend fun updateGeneral(
        release: String,
        pair: CpuPair,
        values: Map<String, Long>,
    ): ProfileConfig

    /** Persists advanced (sparse dotted-path) edits and returns the new config. */
    suspend fun updateAdvanced(
        release: String,
        pair: CpuPair,
        values: Map<String, Long>,
    ): ProfileConfig

    /** Drops every general and advanced override for the release. */
    suspend fun reset(release: String, pair: CpuPair): ProfileConfig

    /** Sets the explicit route; null restores geometry inference. */
    suspend fun updateRoute(release: String, pair: CpuPair, route: String?): ProfileConfig

    /** Sets "fallback_to"; "none" disables, null removes the declaration. */
    suspend fun updateFallback(release: String, pair: CpuPair, fallbackTo: String?): ProfileConfig

    /** Writes the merged profile JSON into the folder the user picked. */
    suspend fun export(release: String, pair: CpuPair, folderUri: String): Boolean

    /** Releases listed by the bundled kernel_profiles/index.conf. */
    suspend fun builtinReleases(): List<String>

    /** Manually selected builtin source, or null for automatic matching. */
    fun activeBuiltinRelease(): String?

    /**
     * Dangerous escape hatch: use another built-in profile as the source for
     * the current device. Null restores automatic matching. Returns the
     * reloaded configuration.
     */
    suspend fun selectBuiltin(
        release: String?,
        deviceRelease: String,
        pair: CpuPair,
    ): ProfileConfig

    /**
     * Typed binary document handed to the native process for the last [load]
     * of this release; null when the controller never resolved it.
     */
    fun nativeDocument(config: ProfileConfig): ByteArray?

    companion object {
        /** Reference templates ("6.6-template"): manually loadable, never auto-matched. */
        const val TemplateSuffix = "-template"
    }
}
