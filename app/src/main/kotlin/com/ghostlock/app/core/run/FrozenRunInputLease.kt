package com.ghostlock.app.core.run

import android.os.Build
import android.os.ParcelFileDescriptor
import android.system.Os
import android.system.OsConstants
import com.ghostlock.app.domain.run.FrozenRunInput
import com.ghostlock.app.domain.run.RunInputArtifact
import com.ghostlock.app.domain.run.RunInputArtifactKind
import java.io.File
import java.io.FileInputStream
import java.nio.file.Files
import java.nio.file.LinkOption
import java.security.MessageDigest
import java.util.UUID

/**
 * One source object opened, identified, and hashed through the same open file description.
 *
 * @property artifact Neutral immutable artifact metadata containing an opaque source token.
 * @property master Master descriptor transferred to [FrozenRunInputLease].
 * @property ownedSource Private source file deleted only after every request duplicate has been created.
 */
data class OpenedRunInputSource(
    val artifact: RunInputArtifact,
    val master: ParcelFileDescriptor,
    val ownedSource: File? = null,
) {
    companion object {
        /**
         * Opens one ordinary file without following links, hashes the same object, and rewinds its shared offset.
         *
         * @param file Exact source selected at the composition boundary.
         * @param logicalId Stable request-global identity.
         * @param kind Fixed artifact kind.
         * @param ownedSource Whether the lease owns deletion of this private source file.
         * @return Opened source ready for lease transfer.
         * @note Robolectric lacks descriptor file-type metadata, so only that host shim may use a no-follow path check; Android always requires descriptor metadata.
         */
        fun open(
            file: File,
            logicalId: String,
            kind: RunInputArtifactKind,
            ownedSource: Boolean = false,
        ): OpenedRunInputSource {
            val raw = Os.open(
                file.absolutePath,
                OsConstants.O_RDONLY or OsConstants.O_CLOEXEC or OsConstants.O_NOFOLLOW,
                0,
            )
            val master = try {
                ParcelFileDescriptor.dup(raw)
            } finally {
                Os.close(raw)
            }
            try {
                val stat = Os.fstat(master.fileDescriptor)
                val descriptorIsRegular = (stat.st_mode and OsConstants.S_IFMT) == OsConstants.S_IFREG
                val hostShimIsRegular = Build.FINGERPRINT == "robolectric" &&
                    Files.isRegularFile(file.toPath(), LinkOption.NOFOLLOW_LINKS)
                require(descriptorIsRegular || hostShimIsRegular) {
                    "run input source is not a regular file"
                }
                val digest = MessageDigest.getInstance("SHA-256")
                var byteCount = 0L
                ParcelFileDescriptor.AutoCloseInputStream(
                    ParcelFileDescriptor.dup(master.fileDescriptor),
                ).use { input ->
                    val expectedSize = if (descriptorIsRegular) stat.st_size else input.channel.size()
                    val buffer = ByteArray(DEFAULT_BUFFER_SIZE)
                    while (true) {
                        val count = input.read(buffer)
                        if (count < 0) break
                        require(byteCount <= expectedSize - count) { "run input source grew while hashing" }
                        digest.update(buffer, 0, count)
                        byteCount += count
                    }
                    require(byteCount == expectedSize) { "run input source changed while hashing" }
                }
                Os.lseek(master.fileDescriptor, 0L, OsConstants.SEEK_SET)
                val token = UUID.randomUUID().toString().replace("-", "")
                return OpenedRunInputSource(
                    artifact = RunInputArtifact(
                        logicalId = logicalId,
                        kind = kind,
                        byteCount = byteCount,
                        sha256 = digest.digest().joinToString(separator = "") { byte ->
                            (byte.toInt() and 0xff).toString(16).padStart(2, '0')
                        },
                        sourceToken = token,
                    ),
                    master = master,
                    ownedSource = file.takeIf { ownedSource },
                )
            } catch (problem: Throwable) {
                runCatching { master.close() }
                throw problem
            }
        }
    }
}

/**
 * Owns the master descriptors behind one [FrozenRunInput] until request assembly finishes.
 *
 * @property input Immutable neutral run facts; source tokens are opaque lookup keys, never paths.
 * @note Closing the lease closes every master first and then deletes only explicitly owned app-private files.
 */
class FrozenRunInputLease(
    val input: FrozenRunInput,
    sources: List<OpenedRunInputSource>,
) : AutoCloseable {
    private val lock = Any()
    private val masters = sources.associate { source -> source.artifact.sourceToken to source.master }
    private val ownedSources = sources.mapNotNull(OpenedRunInputSource::ownedSource)
    private var closed = false

    init {
        require(masters.size == sources.size) { "duplicate opaque source token" }
        require(input.inputArtifacts.map(RunInputArtifact::sourceToken).toSet() == masters.keys) {
            "frozen input and source lease disagree"
        }
    }

    /**
     * Duplicates one master descriptor for transfer to the typed RunHost request.
     *
     * @param sourceToken Opaque token from [RunInputArtifact.sourceToken].
     * @return Caller-owned descriptor referencing the already opened object.
     */
    fun duplicate(sourceToken: String): ParcelFileDescriptor = synchronized(lock) {
        check(!closed) { "run input lease is closed" }
        val master = requireNotNull(masters[sourceToken]) { "unknown run input source token" }
        ParcelFileDescriptor.dup(master.fileDescriptor)
    }

    /** Closes masters exactly once and removes only private owned source copies. */
    override fun close() {
        val closeTargets = synchronized(lock) {
            if (closed) return
            closed = true
            masters.values.toList()
        }
        closeTargets.forEach { descriptor -> runCatching { descriptor.close() } }
        ownedSources.forEach { file -> runCatching { if (file.exists()) check(file.delete()) } }
    }
}
