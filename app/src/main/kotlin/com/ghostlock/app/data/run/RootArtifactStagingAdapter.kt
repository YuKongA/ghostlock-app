package com.ghostlock.app.data.run

import android.os.ParcelFileDescriptor
import android.system.Os
import android.system.OsConstants
import com.ghostlock.app.backend.transport.RunHostArtifactRequest
import com.ghostlock.app.backend.transport.RunHostArtifactKind
import com.ghostlock.app.backend.transport.RunHostIdentity
import com.ghostlock.app.core.flow.PublishedFlowArtifact
import com.ghostlock.app.core.rootscript.PublishedArtifactIdentity
import com.ghostlock.app.core.rootscript.RootScriptArtifact
import com.ghostlock.app.domain.run.RunInputArtifactKind
import java.io.File
import java.io.FileOutputStream
import java.security.MessageDigest

/** Request metadata paired with the read-only descriptor received over AIDL. */
data class OpenedRunInputArtifact(
    val metadata: RunHostArtifactRequest,
    val descriptor: ParcelFileDescriptor,
)

/** One staged ordinary file and the identity captured from the published object. */
data class StagedRunInputArtifact(
    val metadata: RunHostArtifactRequest,
    val file: File,
    val identity: PublishedArtifactIdentity,
)

/**
 * Copies read-only PFD inputs into a host-owned workspace and verifies size/digest/object type.
 *
 * @note Target names derive only from already-validated logical IDs; request paths, modes, owners, and labels
 * are ignored. Every input descriptor is closed after staging.
 */
class RootArtifactStagingAdapter {
    /**
     * Stages and verifies all inputs.
     *
     * @param workspace Fresh host-owned workspace.
     * @param inputs Owned request descriptors.
     * @return Staged files in request order.
     * @throws IllegalArgumentException For non-regular, size, digest, or object identity failures.
     */
    fun stage(
        workspace: AndroidRunWorkspace,
        inputs: List<OpenedRunInputArtifact>,
    ): List<StagedRunInputArtifact> = try {
        inputs.map { input -> stageOne(workspace, input.metadata, input.descriptor) }
    } finally {
        inputs.forEach { input -> runCatching { input.descriptor.close() } }
    }

    /**
     * Publishes the immutable Flow-facing catalog from already staged host-owned objects.
     *
     * @param staged Exact staged records in request order.
     * @return Read-only map keyed by unique request-global logical ID.
     */
    fun publishCatalog(staged: List<StagedRunInputArtifact>): Map<String, PublishedFlowArtifact> =
        staged.associate { artifact ->
            artifact.metadata.logicalId to PublishedFlowArtifact(
                logicalId = artifact.metadata.logicalId,
                kind = artifact.metadata.kind.toDomainKind(),
                path = artifact.file.absolutePath,
                identity = artifact.identity,
            )
        }.also { catalog ->
            require(catalog.size == staged.size) { "duplicate staged artifact identity" }
        }

    /**
     * Publishes the inert script/result pair required by a diagnostic-only Flow input.
     *
     * @param workspace Host workspace.
     * @param identity Run identity.
     * @return Published artifact that is never dispatched as a product root script.
     */
    fun publishDiagnosticArtifact(
        workspace: AndroidRunWorkspace,
        identity: RunHostIdentity,
    ): RootScriptArtifact {
        val script = File(workspace.rootScript, "diagnostic-noop.sh")
        val result = File(workspace.rootScript, "diagnostic-result.empty")
        writeExclusive(script, "#!/system/bin/sh\nexit 0\n".toByteArray(Charsets.US_ASCII), 0b101000000)
        writeExclusive(result, ByteArray(0), 0b110000000)
        return RootScriptArtifact(
            runId = identity.runId,
            generation = identity.generation,
            scriptPath = script.absolutePath,
            resultPath = result.absolutePath,
            scriptIdentity = PublishedArtifactIdentity.capture(script),
            resultIdentity = PublishedArtifactIdentity.capture(result),
            retainedArtifacts = emptyList(),
        )
    }

    /** Copies one descriptor and verifies source metadata and digest while reading it once. */
    private fun stageOne(
        workspace: AndroidRunWorkspace,
        metadata: RunHostArtifactRequest,
        descriptor: ParcelFileDescriptor,
    ): StagedRunInputArtifact {
        val sourceStat = Os.fstat(descriptor.fileDescriptor)
        require((sourceStat.st_mode and OsConstants.S_IFMT) == OsConstants.S_IFREG) {
            "input descriptor is not a regular file"
        }
        require(sourceStat.st_size == metadata.byteCount) { "input descriptor size mismatch" }
        val executable = metadata.kind == RunHostArtifactKind.EXECUTABLE
        val targetDirectory = if (executable) workspace.executables else workspace.stagedInputs
        val target = File(targetDirectory, metadata.logicalId + if (executable) ".exec" else ".bin")
        val finalMode = if (executable) 0b101000000 else 0b100000000
        val digest = MessageDigest.getInstance("SHA-256")
        var copied = 0L
        val targetFd = Os.open(
            target.absolutePath,
            OsConstants.O_CREAT or OsConstants.O_EXCL or OsConstants.O_WRONLY or
                OsConstants.O_CLOEXEC or OsConstants.O_NOFOLLOW,
            finalMode,
        )
        FileOutputStream(targetFd).use { output ->
            val inputDescriptor = ParcelFileDescriptor.dup(descriptor.fileDescriptor)
            ParcelFileDescriptor.AutoCloseInputStream(inputDescriptor).use { input ->
                val buffer = ByteArray(DEFAULT_BUFFER_SIZE)
                while (true) {
                    val count = input.read(buffer)
                    if (count < 0) break
                    require(copied <= metadata.byteCount - count) { "input descriptor grew while staging" }
                    output.write(buffer, 0, count)
                    digest.update(buffer, 0, count)
                    copied += count
                }
                output.flush()
                output.fd.sync()
            }
        }
        require(copied == metadata.byteCount) { "staged byte count mismatch" }
        val actualDigest = digest.digest().joinToString(separator = "") { byte ->
            (byte.toInt() and 0xff).toString(16).padStart(2, '0')
        }
        require(actualDigest == metadata.sha256) { "staged digest mismatch" }
        Os.chmod(target.absolutePath, finalMode)
        val identity = PublishedArtifactIdentity.capture(target)
        require(identity.byteCount == metadata.byteCount && identity.sha256 == metadata.sha256) {
            "published staged artifact identity mismatch"
        }
        return StagedRunInputArtifact(metadata, target, identity)
    }

    /** Writes one new ordinary file, fsyncs it, and applies the final mode. */
    private fun writeExclusive(file: File, bytes: ByteArray, mode: Int) {
        val descriptor = Os.open(
            file.absolutePath,
            OsConstants.O_CREAT or OsConstants.O_EXCL or OsConstants.O_WRONLY or
                OsConstants.O_CLOEXEC or OsConstants.O_NOFOLLOW,
            mode,
        )
        FileOutputStream(descriptor).use { output ->
            output.write(bytes)
            output.flush()
            output.fd.sync()
        }
        Os.chmod(file.absolutePath, mode)
    }

    /** Maps one validated transport artifact kind to the neutral Flow contract. */
    private fun RunHostArtifactKind.toDomainKind(): RunInputArtifactKind = when (this) {
        RunHostArtifactKind.PROFILE -> RunInputArtifactKind.PROFILE
        RunHostArtifactKind.USER_SCRIPT -> RunInputArtifactKind.USER_SCRIPT
        RunHostArtifactKind.EXECUTABLE -> RunInputArtifactKind.EXECUTABLE
        RunHostArtifactKind.LKM -> RunInputArtifactKind.LKM
        RunHostArtifactKind.DATA -> RunInputArtifactKind.DATA
    }
}
