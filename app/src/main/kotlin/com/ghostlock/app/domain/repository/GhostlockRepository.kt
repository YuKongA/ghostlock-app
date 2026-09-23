package com.ghostlock.app.domain.repository

import com.ghostlock.app.domain.model.CpuPair
import com.ghostlock.app.domain.model.DebugSettings
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

    fun setShizukuEnabled(enabled: Boolean)

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
     * Start-up hint: the previous in-process run failed at the W3 seccomp
     * bypass stage, which Shizuku (shell uid, no seccomp) can skip.
     */
    suspend fun lastRunW3SeccompHint(): Boolean

    fun requestShizukuPermission()

    fun setShizukuStatusListener(listener: (() -> Unit)?)

    fun close()
}
