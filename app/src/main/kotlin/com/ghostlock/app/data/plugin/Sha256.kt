package com.ghostlock.app.data.plugin

import java.io.File
import java.security.MessageDigest

/**
 * SHA-256 of a picked plugin module, computed in the App before the native
 * probe runs: the digest is what the probe's --expect-sha256 compares against
 * (the native side hashes BEFORE dlopen), and what the loader re-checks at load
 * time. Lower-case hex, the same spelling the probe and the manifest use.
 *
 * Pure JVM (java.security.MessageDigest), so it is unit-testable with the
 * standard test vectors.
 */
internal object Sha256 {
    private const val BUFFER_BYTES = 64 * 1024

    fun file(file: File): String {
        val digest = MessageDigest.getInstance("SHA-256")
        file.inputStream().use { input ->
            val buffer = ByteArray(BUFFER_BYTES)
            while (true) {
                val read = input.read(buffer)
                if (read <= 0) break
                digest.update(buffer, 0, read)
            }
        }
        return hex(digest.digest())
    }

    fun bytes(value: ByteArray): String =
        hex(MessageDigest.getInstance("SHA-256").digest(value))

    private fun hex(value: ByteArray): String = buildString(value.size * 2) {
        for (byte in value) {
            val v = byte.toInt() and 0xff
            append(HEX[v ushr 4])
            append(HEX[v and 0x0f])
        }
    }

    private const val HEX = "0123456789abcdef"
}
