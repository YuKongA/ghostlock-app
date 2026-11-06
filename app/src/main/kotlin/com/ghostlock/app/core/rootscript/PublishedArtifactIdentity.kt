package com.ghostlock.app.core.rootscript

import android.system.Os
import android.system.OsConstants
import java.io.File
import java.io.FileInputStream
import java.io.ByteArrayOutputStream
import java.security.MessageDigest
import java.nio.file.Files
import java.nio.file.LinkOption

/**
 * Identity of one closed, published ordinary file.
 *
 * @property device Device number obtained from the same opened file descriptor used for hashing.
 * @property inode Inode number obtained from that descriptor.
 * @property byteCount Bytes read from that descriptor.
 * @property sha256 Lowercase SHA-256 of those bytes.
 * @property mode Full stat mode, including regular-file type bits.
 */
data class PublishedArtifactIdentity(
    val device: ULong,
    val inode: ULong,
    val byteCount: Long,
    val sha256: String,
    val mode: UInt,
) {
    /**
     * Verifies object and byte identity through one new `O_NOFOLLOW` opened file descriptor.
     *
     * @param file Candidate artifact path in the verifier's namespace.
     * @return True only when regular-file metadata and bytes match every published field.
     */
    fun matches(file: File): Boolean = runCatching { inspect(file) == this }.getOrDefault(false)

    /**
     * Verifies the pre-created result object while allowing its bytes to change during execution.
     *
     * @param file Candidate result path.
     * @return True when the same regular-file object and mode are still reachable without following links.
     */
    fun matchesObject(file: File): Boolean = runCatching {
        val current = inspect(file)
        current.device == device && current.inode == inode && current.mode == mode
    }.getOrDefault(false)

    /**
     * Reads the changing result bytes from the same descriptor whose object identity is checked.
     *
     * @param file Candidate result path.
     * @return Bytes when device, inode, and mode match; null on replacement, links, or I/O failure.
     */
    fun readSameObject(file: File): ByteArray? = runCatching {
        val descriptor = Os.open(
            file.absolutePath,
            OsConstants.O_RDONLY or OsConstants.O_CLOEXEC or OsConstants.O_NOFOLLOW,
            0,
        )
        FileInputStream(descriptor).use { input ->
            val stat = Os.fstat(input.fd)
            val metadata = metadata(file, stat.st_dev, stat.st_ino, stat.st_mode, stat.st_size)
            require(metadata.device == device && metadata.inode == inode)
            require(metadata.mode == mode)
            val output = ByteArrayOutputStream()
            val buffer = ByteArray(DEFAULT_BUFFER_SIZE)
            while (true) {
                val count = input.read(buffer)
                if (count < 0) break
                output.write(buffer, 0, count)
            }
            val bytes = output.toByteArray()
            require(bytes.size.toLong() == metadata.size)
            bytes
        }
    }.getOrNull()

    companion object {
        /**
         * Captures metadata and digest from the same `O_NOFOLLOW` opened descriptor.
         *
         * @param file Closed published artifact.
         * @return Immutable identity.
         * @throws IllegalArgumentException When the path is not an ordinary file.
         */
        fun capture(file: File): PublishedArtifactIdentity = inspect(file)

        /** Opens, stats, and hashes a single descriptor without path re-open races. */
        private fun inspect(file: File): PublishedArtifactIdentity {
            val descriptor = Os.open(
                file.absolutePath,
                OsConstants.O_RDONLY or OsConstants.O_CLOEXEC or OsConstants.O_NOFOLLOW,
                0,
            )
            return FileInputStream(descriptor).use { input ->
                val stat = Os.fstat(input.fd)
                val metadata = metadata(file, stat.st_dev, stat.st_ino, stat.st_mode, stat.st_size)
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

        /** Uses descriptor stat on Android and a JVM Unix-attribute fallback only for host test shims. */
        private fun metadata(
            file: File,
            statDevice: Long,
            statInode: Long,
            statMode: Int,
            statSize: Long,
        ): Metadata {
            if ((statMode and OsConstants.S_IFMT) == OsConstants.S_IFREG) {
                return Metadata(
                    device = statDevice.toULong(),
                    inode = statInode.toULong(),
                    mode = statMode.toUInt(),
                    size = statSize,
                )
            }
            val path = file.toPath()
            require(Files.isRegularFile(path, LinkOption.NOFOLLOW_LINKS)) { "artifact is not a regular file" }
            return Metadata(
                device = (Files.getAttribute(path, "unix:dev", LinkOption.NOFOLLOW_LINKS) as Number).toLong().toULong(),
                inode = (Files.getAttribute(path, "unix:ino", LinkOption.NOFOLLOW_LINKS) as Number).toLong().toULong(),
                mode = (Files.getAttribute(path, "unix:mode", LinkOption.NOFOLLOW_LINKS) as Number).toLong().toUInt(),
                size = Files.size(path),
            )
        }

        /** Minimal metadata needed to bind the descriptor bytes to one ordinary object. */
        private data class Metadata(
            val device: ULong,
            val inode: ULong,
            val mode: UInt,
            val size: Long,
        )
    }
}
