package com.ghostlock.app.data.plugin

import android.os.Build
import android.system.Os
import android.system.OsConstants
import android.util.AtomicFile
import com.ghostlock.plugin.core.PluginCatalogSnapshot
import com.ghostlock.plugin.core.PluginContentSha256
import com.ghostlock.plugin.core.PluginMetadataCodec
import java.io.File
import java.io.FileInputStream
import java.io.FileOutputStream
import java.io.InputStream
import java.nio.file.Files
import java.nio.file.LinkOption
import java.nio.file.StandardCopyOption
import java.nio.file.attribute.PosixFilePermission
import java.util.UUID

/** One object reopened from its content-addressed path and fully reverified. */
internal data class StoredPluginObject(
    val file: File,
    val verified: VerifiedPluginArchive,
)

/**
 * Sole physical owner of the app-private plugin catalog.
 *
 * Objects are published before the AtomicFile catalog can reference them. Every read used for a
 * READY publication reopens and semantically verifies each active object; a content-addressed file
 * name is never treated as proof of its bytes.
 */
internal class AndroidPluginCatalogStore(
    private val rootDirectory: File,
    private val verifier: AndroidPluginArchiveVerifier = AndroidPluginArchiveVerifier(),
) {
    private val objectsDirectory = File(rootDirectory, "objects/sha256")
    private val stagingDirectory = File(rootDirectory, "staging")
    private val catalogFile = AtomicFile(File(rootDirectory, "catalog.json"))

    /** Loads the committed catalog and reverifies every referenced immutable object. */
    fun readVerifiedSnapshot(): PluginCatalogSnapshot {
        ensureLayout()
        val snapshot = if (!catalogFile.baseFile.exists()) {
            PluginCatalogSnapshot.empty()
        } else {
            val source = catalogFile.openRead().use(FileInputStream::readBytes)
            PluginMetadataCodec.decodeCatalogSnapshot(source.decodeToString())
        }
        snapshot.activeRecords.forEach { record ->
            val stored = reopenVerifiedObject(record.pluginContentSha256)
            require(stored.verified.manifest == record.manifest.normalized()) {
                "stored plugin manifest does not match catalog projection"
            }
        }
        return snapshot
    }

    /**
     * Streams, fsyncs and verifies one candidate before publishing it as a 0400 object.
     *
     * If the same semantic object already exists, its current bytes are reverified and retained;
     * a differently packed candidate never replaces them.
     */
    fun installObject(
        source: InputStream,
        expectedContentRoot: PluginContentSha256,
    ): StoredPluginObject {
        ensureLayout()
        val token = UUID.randomUUID().toString()
        val staged = File(stagingDirectory, "$token.glplugin.zip")
        val scratch = File(stagingDirectory, "$token.scratch")
        require(scratch.mkdir()) { "cannot create plugin verification scratch directory" }
        try {
            writeExclusive(staged, source)
            val candidate = verifier.verify(staged, scratch, expectedContentRoot)
            val target = objectFile(expectedContentRoot)
            if (Files.exists(target.toPath(), LinkOption.NOFOLLOW_LINKS)) {
                val existing = reopenVerifiedObject(expectedContentRoot)
                require(existing.verified.manifest == candidate.manifest) {
                    "same semantic content root resolved to different manifests"
                }
                require(staged.delete()) { "cannot remove redundant staged plugin object" }
                return existing
            }

            applyMode(staged, OBJECT_MODE)
            Files.move(staged.toPath(), target.toPath(), StandardCopyOption.ATOMIC_MOVE)
            fsyncDirectory(objectsDirectory)
            val published = reopenVerifiedObject(expectedContentRoot)
            require(published.verified.manifest == candidate.manifest) {
                "published plugin object changed before reopen verification"
            }
            return published
        } finally {
            if (staged.exists()) staged.delete()
            deleteScratchDirectory(scratch)
        }
    }

    /** Atomically commits one already-normalized snapshot after all referenced objects verify. */
    fun commit(snapshot: PluginCatalogSnapshot) {
        ensureLayout()
        val normalized = snapshot.normalized()
        normalized.activeRecords.forEach { record ->
            val stored = reopenVerifiedObject(record.pluginContentSha256)
            require(stored.verified.manifest == record.manifest) {
                "cannot commit a catalog projection that differs from its object"
            }
        }
        val bytes = PluginMetadataCodec.encodeCatalogSnapshot(normalized).encodeToByteArray()
        val output = catalogFile.startWrite()
        try {
            output.write(bytes)
            output.flush()
            catalogFile.finishWrite(output)
        } catch (problem: Throwable) {
            catalogFile.failWrite(output)
            throw problem
        }
        applyMode(catalogFile.baseFile, CATALOG_MODE)
        fsyncDirectory(rootDirectory)
    }

    /** Removes only well-formed content-addressed objects not referenced by [snapshot]. */
    fun deleteUnreferencedObjects(snapshot: PluginCatalogSnapshot) {
        ensureLayout()
        val referenced = snapshot.normalized().activeRecords
            .mapTo(mutableSetOf()) { objectFileName(it.pluginContentSha256) }
        objectsDirectory.listFiles().orEmpty().forEach { candidate ->
            if (candidate.name !in referenced && OBJECT_FILE_PATTERN.matches(candidate.name)) {
                require(Files.isRegularFile(candidate.toPath(), LinkOption.NOFOLLOW_LINKS)) {
                    "plugin object candidate is not a regular file"
                }
                Files.delete(candidate.toPath())
            }
        }
        fsyncDirectory(objectsDirectory)
    }

    /** Reopens one exact object and repeats format, CRC, file-hash and semantic-root checks. */
    internal fun reopenVerifiedObject(contentRoot: PluginContentSha256): StoredPluginObject {
        ensureLayout()
        val file = objectFile(contentRoot)
        require(Files.isRegularFile(file.toPath(), LinkOption.NOFOLLOW_LINKS)) {
            "catalog plugin object is missing or not a regular file"
        }
        requireObjectMode(file)
        val scratch = File(stagingDirectory, "reopen-${UUID.randomUUID()}.scratch")
        require(scratch.mkdir()) { "cannot create plugin reopen scratch directory" }
        return try {
            StoredPluginObject(file, verifier.verify(file, scratch, contentRoot))
        } finally {
            deleteScratchDirectory(scratch)
        }
    }

    private fun ensureLayout() {
        ensureDirectory(rootDirectory)
        ensureDirectory(File(rootDirectory, "objects"))
        ensureDirectory(objectsDirectory)
        ensureDirectory(stagingDirectory)
    }

    private fun ensureDirectory(directory: File) {
        if (!Files.exists(directory.toPath(), LinkOption.NOFOLLOW_LINKS)) {
            Files.createDirectory(directory.toPath())
        }
        require(Files.isDirectory(directory.toPath(), LinkOption.NOFOLLOW_LINKS)) {
            "plugin catalog path is not a directory: ${directory.name}"
        }
        applyMode(directory, DIRECTORY_MODE)
    }

    private fun objectFile(contentRoot: PluginContentSha256): File =
        File(objectsDirectory, objectFileName(contentRoot))

    private fun writeExclusive(target: File, source: InputStream) {
        val descriptor = Os.open(
            target.absolutePath,
            OsConstants.O_CREAT or OsConstants.O_EXCL or OsConstants.O_WRONLY or
                OsConstants.O_CLOEXEC or OsConstants.O_NOFOLLOW,
            STAGING_MODE,
        )
        try {
            FileOutputStream(descriptor).use { output ->
                val buffer = ByteArray(STREAM_BUFFER_BYTES)
                while (true) {
                    val count = source.read(buffer)
                    if (count < 0) break
                    if (count == 0) continue
                    output.write(buffer, 0, count)
                }
                output.flush()
                output.fd.sync()
            }
        } catch (problem: Throwable) {
            target.delete()
            throw problem
        }
    }

    private fun requireObjectMode(file: File) {
        val mode = if (Build.FINGERPRINT == "robolectric") {
            (Files.getAttribute(file.toPath(), "unix:mode", LinkOption.NOFOLLOW_LINKS) as Number)
                .toInt()
        } else {
            Os.lstat(file.absolutePath).st_mode
        }
        require(mode and OsConstants.S_IFMT == OsConstants.S_IFREG) {
            "catalog plugin object is not a regular file"
        }
        require(mode and 0b111111111 == OBJECT_MODE) {
            "catalog plugin object mode must be 0400"
        }
    }

    /** Applies Android mode and mirrors it on Robolectric's host filesystem. */
    private fun applyMode(file: File, mode: Int) {
        Os.chmod(file.absolutePath, mode)
        if (Build.FINGERPRINT == "robolectric") {
            Files.setPosixFilePermissions(file.toPath(), mode.toPermissions())
        }
    }

    private fun fsyncDirectory(directory: File) {
        if (Build.FINGERPRINT == "robolectric") return
        val descriptor = Os.open(
            directory.absolutePath,
            OsConstants.O_RDONLY or OsConstants.O_CLOEXEC or OsConstants.O_NOFOLLOW,
            0,
        )
        try {
            Os.fsync(descriptor)
        } finally {
            Os.close(descriptor)
        }
    }

    private fun deleteScratchDirectory(directory: File) {
        directory.listFiles().orEmpty().forEach { child ->
            require(child.isFile && child.delete()) { "cannot remove plugin verifier scratch entry" }
        }
        if (directory.exists()) require(directory.delete()) { "cannot remove plugin verifier scratch directory" }
    }

    private fun Int.toPermissions(): Set<PosixFilePermission> = buildSet {
        if (this@toPermissions and 0b100000000 != 0) add(PosixFilePermission.OWNER_READ)
        if (this@toPermissions and 0b010000000 != 0) add(PosixFilePermission.OWNER_WRITE)
        if (this@toPermissions and 0b001000000 != 0) add(PosixFilePermission.OWNER_EXECUTE)
        if (this@toPermissions and 0b000100000 != 0) add(PosixFilePermission.GROUP_READ)
        if (this@toPermissions and 0b000010000 != 0) add(PosixFilePermission.GROUP_WRITE)
        if (this@toPermissions and 0b000001000 != 0) add(PosixFilePermission.GROUP_EXECUTE)
        if (this@toPermissions and 0b000000100 != 0) add(PosixFilePermission.OTHERS_READ)
        if (this@toPermissions and 0b000000010 != 0) add(PosixFilePermission.OTHERS_WRITE)
        if (this@toPermissions and 0b000000001 != 0) add(PosixFilePermission.OTHERS_EXECUTE)
    }

    private companion object {
        const val STREAM_BUFFER_BYTES = 16 * 1024
        const val DIRECTORY_MODE = 0b111000000
        const val STAGING_MODE = 0b110000000
        const val OBJECT_MODE = 0b100000000
        const val CATALOG_MODE = 0b110000000
        val OBJECT_FILE_PATTERN = Regex("[0-9a-f]{64}\\.glplugin\\.zip")

        fun objectFileName(contentRoot: PluginContentSha256): String =
            "$contentRoot.glplugin.zip"
    }
}
