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
        val ok = nativeWriteBlock(handle, path, offset, iv, sequence)
        if (!ok) log.debug("NativeRawEsp.writeBlock failed offset=" + offset)
        return ok
    }

    fun writeBlockViaHelper(
        handle: Long, path: String, offset: Long, iv: ByteArray, sequence: Int, helperPath: String,
    ): Boolean {
        val ok = nativeWriteBlockViaHelper(handle, path, offset, iv, sequence, helperPath)
        if (!ok) log.debug("NativeRawEsp.writeBlockViaHelper failed offset=" + offset)
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
