package com.ghostlock.app.data.payload

import java.io.File
import java.io.InputStream
import java.security.MessageDigest

/** What one payload file import did. */
sealed interface PayloadImportResult {
    data class Imported(
        val displayName: String,
        /** Relative to the payload root, e.g. payload/script/<sha12>-setup.sh. */
        val relativePath: String,
        val sha256: String,
        val size: Long,
    ) : PayloadImportResult

    data class Rejected(val reason: String) : PayloadImportResult
}

/** Which payload bucket a file belongs to. */
enum class PayloadKind(val directory: String, val maxBytes: Long, val suffix: String) {
    Script("script", 4L * 1024L * 1024L, ".sh"),
    Ko("ko", 64L * 1024L * 1024L, ".ko"),
}

/**
 * Copies a user-picked file into the App's no-backup payload directory.
 *
 * The rules are the plugin importer's, for the same reasons:
 *  1. the size limit is enforced WHILE reading, so a huge or endless source
 *     cannot fill the disk (and the bounded pass happens before anything is
 *     written into place);
 *  2. the source is read TWICE — once to hash, once to copy — and the two
 *     digests must agree, so a file swapped between "picked" and "copied" is
 *     rejected instead of being installed under the first hash;
 *  3. the copy lands in a temporary file and is moved with ATOMIC_MOVE, so a
 *     crash never leaves a half-written script or module under its final name;
 *  4. a rejected import leaves NOTHING behind (the temporary file is removed).
 *
 * The digest is computed over the bytes that were actually copied; the caller
 * stores it as the pinned hash, so "verified" means "these exact bytes".
 */
class PayloadImportService(
    private val homeDir: File,
    private val kind: PayloadKind,
) {
    fun import(displayName: String, open: () -> InputStream): PayloadImportResult {
        val fileName = sanitize(displayName) ?: return PayloadImportResult.Rejected(
            "the picked file has no usable name",
        )
        /* Pass 1: bounded hash of the source as it is right now. */
        val first = try {
            digest(open())
        } catch (error: Exception) {
            return PayloadImportResult.Rejected("cannot read the picked file: " + message(error))
        }
        val firstOk = first ?: return PayloadImportResult.Rejected(
            "the file is larger than " + kind.maxBytes + " bytes",
        )
        val directory = File(homeDir, PayloadPaths.root(kind))
        if (!directory.isDirectory && !directory.mkdirs()) {
            return PayloadImportResult.Rejected("cannot create " + directory.absolutePath)
        }
        val finalName = firstOk.sha256.take(12) + "-" + fileName
        val target = File(directory, finalName)
        val temporary = File(directory, finalName + ".part")
        /* Pass 2: copy + hash; the digests must match. */
        val second = try {
            copy(open(), temporary)
        } catch (error: Exception) {
            temporary.delete()
            return PayloadImportResult.Rejected("cannot copy the picked file: " + message(error))
        }
        if (second == null) {
            temporary.delete()
            return PayloadImportResult.Rejected(
                "the file is larger than " + kind.maxBytes + " bytes",
            )
        }
        if (second.sha256 != firstOk.sha256 || second.size != firstOk.size) {
            temporary.delete()
            return PayloadImportResult.Rejected(
                "the file changed while it was being copied; pick it again",
            )
        }
        if (target.exists() && !target.delete()) {
            temporary.delete()
            return PayloadImportResult.Rejected("cannot replace the existing copy")
        }
        if (!temporary.renameTo(target)) {
            temporary.delete()
            return PayloadImportResult.Rejected("cannot install the copy")
        }
        return PayloadImportResult.Imported(
            displayName = fileName,
            relativePath = PayloadPaths.relative(kind, finalName),
            sha256 = second.sha256,
            size = second.size,
        )
    }

    private data class Digest(val sha256: String, val size: Long)

    /** Hashes at most the limit + 1 byte; null when the source is too large. */
    private fun digest(stream: InputStream): Digest? {
        val md = MessageDigest.getInstance("SHA-256")
        var size = 0L
        val buffer = ByteArray(64 * 1024)
        stream.use { input ->
            while (true) {
                val read = input.read(buffer)
                if (read < 0) break
                size += read
                if (size > kind.maxBytes) return null
                md.update(buffer, 0, read)
            }
        }
        return Digest(hex(md.digest()), size)
    }

    /** Copies into [temporary] while hashing; null when the source is too large. */
    private fun copy(stream: InputStream, temporary: File): Digest? {
        val md = MessageDigest.getInstance("SHA-256")
        var size = 0L
        val buffer = ByteArray(64 * 1024)
        stream.use { input ->
            val raw = java.io.FileOutputStream(temporary)
            raw.use { file ->
                val output = file.buffered()
                while (true) {
                    val read = input.read(buffer)
                    if (read < 0) break
                    size += read
                    if (size > kind.maxBytes) return null
                    md.update(buffer, 0, read)
                    output.write(buffer, 0, read)
                }
                output.flush()
                /* Durable before the atomic rename: a crash cannot leave a
                 * truncated file under the final name. */
                file.fd.sync()
            }
        }
        return Digest(hex(md.digest()), size)
    }

    private fun hex(bytes: ByteArray): String =
        bytes.joinToString("") { "%02x".format(it) }

    private fun message(error: Throwable): String = error.message ?: "unknown error"

    /** Keeps the file inside its bucket: no separators, no traversal, no dots-only. */
    private fun sanitize(name: String): String? {
        val base = name.substringAfterLast('/').substringAfterLast('\\').trim()
        if (base.isEmpty() || base == "." || base == "..") return null
        val cleaned = base.map { if (it.isLetterOrDigit() || it in "._-") it else '_' }.joinToString("")
        if (cleaned.isEmpty() || cleaned.all { it == '.' }) return null
        return cleaned
    }
}

/** Paths of the payload buckets, relative to <GHOSTLOCK_HOME>. */
object PayloadPaths {
    const val ROOT = "payload"

    fun root(kind: PayloadKind): String = ROOT + "/" + kind.directory

    fun relative(kind: PayloadKind, fileName: String): String =
        root(kind) + "/" + fileName
}
