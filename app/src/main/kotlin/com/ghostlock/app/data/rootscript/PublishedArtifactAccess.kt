package com.ghostlock.app.data.rootscript

import android.system.Os
import android.system.OsConstants
import android.system.StructStat
import android.os.Build
import com.ghostlock.app.core.rootscript.PublishedArtifactIdentity
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.FileInputStream
import java.nio.file.Files
import java.nio.file.LinkOption
import java.security.MessageDigest

/** Android ordinary-file identity capture and verification through no-follow descriptors. */
object PublishedArtifactAccess {
    /**
     * Captures metadata and digest from one `O_NOFOLLOW` descriptor.
     *
     * @param file Closed published artifact.
     * @return Immutable physical and byte identity.
     * @throws IllegalArgumentException When the path is not an ordinary file or changes while hashing.
     */
    fun capture(file: File): PublishedArtifactIdentity = inspect(file)

    /** @return True only when current metadata and bytes still match [identity]. */
    fun matches(identity: PublishedArtifactIdentity, file: File): Boolean = try {
        inspect(file) == identity
    } catch (_: Exception) {
        false
    }

    /** @return True when the same ordinary object and mode remain reachable without following links. */
    fun matchesObject(identity: PublishedArtifactIdentity, file: File): Boolean = try {
        val descriptor = Os.open(
            file.absolutePath,
            OsConstants.O_RDONLY or OsConstants.O_CLOEXEC or OsConstants.O_NOFOLLOW,
            0,
        )
        FileInputStream(descriptor).use { input ->
            matchesObject(identity, metadata(file, Os.fstat(input.fd)))
        }
    } catch (_: Exception) {
        false
    }

    /**
     * Verifies and reads one mutable result through the same bounded descriptor.
     *
     * @param file Result path in the current RunHost namespace.
     * @param identity Pre-execution object identity.
     * @param maximumByteCount Inclusive protocol byte limit.
     * @return Phase-aware physical result; expected failures never escape.
     */
    internal fun readSameObject(
        file: File,
        identity: PublishedArtifactIdentity,
        maximumByteCount: Int,
    ): SameObjectRead {
        require(maximumByteCount > 0) { "maximum byte count must be positive" }
        val descriptor = try {
            Os.open(
                file.absolutePath,
                OsConstants.O_RDONLY or OsConstants.O_CLOEXEC or OsConstants.O_NOFOLLOW,
                0,
            )
        } catch (_: Exception) {
            return SameObjectRead.IdentityMismatch
        }
        val input = try {
            FileInputStream(descriptor)
        } catch (_: Exception) {
            try {
                Os.close(descriptor)
            } catch (_: Exception) {
            }
            return SameObjectRead.IdentityMismatch
        }
        var identityConfirmed = false
        var closeFailed = false
        val outcome = try {
            val metadata = metadata(file, Os.fstat(input.fd))
            if (!matchesObject(identity, metadata)) {
                SameObjectRead.IdentityMismatch
            } else {
                identityConfirmed = true
                readBounded(input, metadata.size, maximumByteCount)
            }
        } catch (_: Exception) {
            if (identityConfirmed) SameObjectRead.ReadFailure else SameObjectRead.IdentityMismatch
        } finally {
            try {
                input.close()
            } catch (_: Exception) {
                closeFailed = true
            }
        }
        return if (closeFailed) {
            if (identityConfirmed) SameObjectRead.ReadFailure else SameObjectRead.IdentityMismatch
        } else {
            outcome
        }
    }

    /** @return True when descriptor metadata denotes the same captured object and mode. */
    internal fun matchesObject(
        identity: PublishedArtifactIdentity,
        metadata: PublishedArtifactMetadata,
    ): Boolean = metadata.regular &&
        metadata.device == identity.device &&
        metadata.inode == identity.inode &&
        metadata.mode == identity.mode

    /** Returns descriptor metadata and permits path-based Unix attributes only for the Robolectric host shim. */
    internal fun metadata(
        file: File,
        stat: StructStat,
        allowHostPathFallback: Boolean = Build.FINGERPRINT == "robolectric",
    ): PublishedArtifactMetadata {
        if ((stat.st_mode and OsConstants.S_IFMT) == OsConstants.S_IFREG) {
            return PublishedArtifactMetadata(
                regular = true,
                device = stat.st_dev.toULong(),
                inode = stat.st_ino.toULong(),
                mode = stat.st_mode.toUInt(),
                size = stat.st_size,
            )
        }
        if (!allowHostPathFallback) {
            return PublishedArtifactMetadata(
                regular = false,
                device = stat.st_dev.toULong(),
                inode = stat.st_ino.toULong(),
                mode = stat.st_mode.toUInt(),
                size = stat.st_size,
            )
        }
        val path = file.toPath()
        val regular = Files.isRegularFile(path, LinkOption.NOFOLLOW_LINKS)
        if (!regular) {
            return PublishedArtifactMetadata(false, 0UL, 0UL, 0U, 0L)
        }
        return PublishedArtifactMetadata(
            regular = true,
            device = (Files.getAttribute(path, "unix:dev", LinkOption.NOFOLLOW_LINKS) as Number)
                .toLong().toULong(),
            inode = (Files.getAttribute(path, "unix:ino", LinkOption.NOFOLLOW_LINKS) as Number)
                .toLong().toULong(),
            mode = (Files.getAttribute(path, "unix:mode", LinkOption.NOFOLLOW_LINKS) as Number)
                .toLong().toUInt(),
            size = Files.size(path),
        )
    }

    /** Reads the identity-confirmed descriptor without allocating or accepting more than [maximumByteCount]. */
    private fun readBounded(
        input: FileInputStream,
        expectedSize: Long,
        maximumByteCount: Int,
    ): SameObjectRead {
        if (expectedSize < 0L || expectedSize > maximumByteCount.toLong()) {
            return SameObjectRead.ReadFailure
        }
        return try {
            val output = ByteArrayOutputStream(expectedSize.toInt())
            val buffer = ByteArray(DEFAULT_BUFFER_SIZE)
            var byteCount = 0
            while (true) {
                val count = input.read(buffer)
                if (count < 0) break
                if (byteCount > maximumByteCount - count) return SameObjectRead.ReadFailure
                output.write(buffer, 0, count)
                byteCount += count
            }
            if (byteCount.toLong() != expectedSize) {
                SameObjectRead.ReadFailure
            } else {
                SameObjectRead.Complete(output.toByteArray())
            }
        } catch (_: Exception) {
            SameObjectRead.ReadFailure
        }
    }

    /** Opens, stats, and hashes one descriptor without path re-open races. */
    private fun inspect(file: File): PublishedArtifactIdentity {
        val descriptor = Os.open(
            file.absolutePath,
            OsConstants.O_RDONLY or OsConstants.O_CLOEXEC or OsConstants.O_NOFOLLOW,
            0,
        )
        return FileInputStream(descriptor).use { input ->
            val metadata = metadata(file, Os.fstat(input.fd))
            require(metadata.regular) { "artifact is not a regular file" }
            val digest = MessageDigest.getInstance("SHA-256")
            val buffer = ByteArray(DEFAULT_BUFFER_SIZE)
            var byteCount = 0L
            while (true) {
                val count = input.read(buffer)
                if (count < 0) break
                digest.update(buffer, 0, count)
                byteCount += count
            }
            require(byteCount == metadata.size) { "artifact changed while hashing" }
            PublishedArtifactIdentity(
                device = metadata.device,
                inode = metadata.inode,
                byteCount = byteCount,
                sha256 = digest.digest().joinToString(separator = "") { byte ->
                    (byte.toInt() and 0xff).toString(16).padStart(2, '0')
                },
                mode = metadata.mode,
            )
        }
    }
}

/** Descriptor metadata needed to bind bytes to one ordinary object. */
internal data class PublishedArtifactMetadata(
    val regular: Boolean,
    val device: ULong,
    val inode: ULong,
    val mode: UInt,
    val size: Long,
)

/** Physical result of one bounded same-object read. */
internal sealed interface SameObjectRead {
    /** Opened path did not resolve to the captured ordinary object and mode. */
    data object IdentityMismatch : SameObjectRead

    /** Identity matched, but bounded reading or size stability failed. */
    data object ReadFailure : SameObjectRead

    /** Complete bytes read from the checked descriptor. */
    data class Complete(val bytes: ByteArray) : SameObjectRead
}
