package com.ghostlock.app.domain.repository

import com.ghostlock.app.domain.model.CpuPair
import com.ghostlock.app.domain.model.DebugSettings
import com.ghostlock.app.domain.model.KernelSnapshot
import com.ghostlock.app.domain.model.OffsetCandidate
import com.ghostlock.app.domain.model.OffsetImportResult
import com.ghostlock.app.domain.model.ParseResult

interface GhostlockRepository {
    suspend fun snapshot(): KernelSnapshot

    fun selectCpuPair(index: Int)

    fun setSafeModeEnabled(enabled: Boolean)

    fun setShizukuEnabled(enabled: Boolean)

    suspend fun exportCandidates(): List<OffsetCandidate>

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
