package com.ghostlock.app.domain.repository

import com.ghostlock.app.data.component.BackendKind
import com.ghostlock.app.data.component.CombinationSpec
import com.ghostlock.app.data.plugin.PluginDescriptor
import com.ghostlock.app.data.plugin.PluginImportResult
import com.ghostlock.app.data.plugin.PluginManifestEntry
import com.ghostlock.app.data.plugin.PluginValue
import com.ghostlock.app.domain.model.CpuPair
import com.ghostlock.app.domain.model.DebugSettings
import com.ghostlock.app.domain.model.ExecutionMode
import com.ghostlock.app.domain.model.KernelSnapshot
import com.ghostlock.app.domain.model.OffsetCandidate
import com.ghostlock.app.domain.model.OffsetImportResult
import com.ghostlock.app.domain.model.ParseResult
import com.ghostlock.app.domain.model.ProfileConfig
import com.ghostlock.app.domain.model.UserProfileFile

interface GhostlockRepository {
    suspend fun snapshot(): KernelSnapshot

    fun selectCpuPair(index: Int)

    fun setSafeModeEnabled(enabled: Boolean)

    /** Skip the pre-attack KernelSU check and run the exploit as a test. */
    fun setForceAttackTest(enabled: Boolean)

    /** Persists the entry/execution selection (unavailable modes are ignored). */
    fun setExecutionMode(mode: ExecutionMode)

    /** Persists the header backend selection (unavailable backends are ignored). */
    fun setBackendKind(kind: BackendKind)

    /**
     * Persists the single combination token; this is the selection authority.
     * Backend and execution mode are derived from it for compat. Planned
     * (unavailable) combinations are ignored.
     */
    fun setCombination(spec: CombinationSpec)

    /** The restored/current combination, used to seed the UI on start-up. */
    fun currentCombination(): CombinationSpec

    /**
     * Imports one or more picked documents (file name -> text). Includes are
     * resolved against the picked files first, then the bundled assets; a
     * missing include fails the import so the user can pick it too.
     */
    suspend fun importOffsets(documents: Map<String, String>): OffsetImportResult

    suspend fun confirmImport(documents: Map<String, String>): OffsetImportResult

    suspend fun parseSource(
        input: String,
        xblPath: String? = null,
        uefiPath: String? = null,
        overwrite: Boolean = false,
        onLog: (String) -> Unit = {},
    ): ParseResult

    suspend fun readDocument(uri: String): String

    suspend fun cacheDocument(uri: String, fileName: String): String

    suspend fun publishOffsets(candidate: OffsetCandidate): String

    /** Verbatim user-imported documents, newest first. */
    suspend fun userProfiles(): List<UserProfileFile>

    suspend fun deleteUserProfile(name: String): Boolean

    /** Returns the resulting file name, or null when the rename failed. */
    suspend fun renameUserProfile(name: String, newName: String): String?

    /** Renders a stored document as HOCON, writes it to Downloads and shares it. */
    suspend fun exportUserProfile(name: String): String

    /**
     * Converts a legacy stored document into the current layout, stores the
     * result as a new user document and returns its file name.
     */
    suspend fun convertUserProfile(name: String): String?

    /**
     * Opens an isolated editing session for the saved profile [name] (null for
     * the current builtin source) and returns its resolved configuration. The
     * session never touches the live attack controller until committed.
     */
    suspend fun beginEditSession(name: String?): ProfileConfig?

    /** Controller of the open editing session, null when none is open. */
    fun editSessionController(): ProfileConfigController?

    /** True when the session edits the profile the attack controller loads. */
    fun editSessionIsLive(): Boolean

    /** File name edited by the open session; null when it edits the builtin. */
    fun editSessionTarget(): String?

    /** Copies the session overrides into the live controller. */
    suspend fun commitEditSession(): Boolean

    /** Updates the edited document in place with the session result. */
    suspend fun saveEditSessionInPlace(): Boolean

    /** Stores the session result as a new saved profile; returns its name. */
    suspend fun saveEditSessionAsNew(): String?

    /** Renders the session result as HOCON, writes it to Downloads, shares it. */
    suspend fun exportEditSession(): String

    /** Release the editing session targets; a stored document uses its own. */
    fun editSessionRelease(): String?

    fun endEditSession()

    /** Single authority for loading, editing and exporting profile config. */
    fun profileController(): ProfileConfigController

    suspend fun debugSettings(): DebugSettings

    fun setDebugExportEnabled(enabled: Boolean)

    fun setDebugExportLocation(location: String)

    fun setDebugKernelLogEnabled(enabled: Boolean)

    suspend fun runExploit(pair: CpuPair, onLog: (String) -> Unit): Int

    suspend fun runExploitWithShizuku(pair: CpuPair, onLog: (String) -> Unit): Int

    /**
     * The step left `in_progress` by the previous run (usually a kernel panic),
     * or null when the last run completed/failed cleanly. `w3*` means Shizuku
     * (shell uid, no seccomp) can skip that stage.
     */
    suspend fun lastRunStuckStep(): String?

    fun requestShizukuPermission()

    fun setShizukuStatusListener(listener: (() -> Unit)?)

    /**
     * P1: the imported-plugin registry (no-backup root). The rows are the
     * App-side record of what was imported; the plugin's own schema comes from
     * the native probe, never from here.
     */
    suspend fun pluginEntries(): List<PluginManifestEntry>

    /** Enables or disables an imported plugin; returns the refreshed registry. */
    suspend fun setPluginEnabled(id: String, enabled: Boolean): List<PluginManifestEntry>

    /**
     * P1: stages the picked document, hashes it, describes it with the native
     * probe under that digest, and installs it under the no-backup root. A
     * rejection installs nothing.
     */
    suspend fun importPlugin(uri: String, displayName: String?): PluginImportResult

    /**
     * Re-describes the installed modules with the native probe, keyed by id.
     * A module the probe cannot describe is simply absent (the page greys it).
     */
    suspend fun describePlugins(): Map<String, PluginDescriptor>

    /** P1: the explicit parameter overrides of every described plugin. */
    suspend fun pluginParamOverrides(): Map<String, Map<String, PluginValue>>

    /**
     * P1: stores one plugin parameter override (dotted path in the existing
     * advanced override tree); null clears it so the descriptor default applies.
     */
    suspend fun setPluginParam(id: String, name: String, value: PluginValue?)

    /** True when the native probe binary is present, so importing can be offered. */
    fun pluginImportAvailable(): Boolean

    fun close()
}
