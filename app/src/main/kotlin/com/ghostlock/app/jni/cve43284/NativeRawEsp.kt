package com.ghostlock.app.jni.cve43284

/** 薄 JNI：只暴露 DFRoot 同款 syscall 序列，不做 plan/IV/校验。 */
object NativeRawEsp {
    init {
        System.loadLibrary("ghostlock_jni")
    }

    external fun open(spi: Int, encapPort: Int, senderPort: Int): Long
    external fun writeBlock(handle: Long, path: String, offset: Long, iv: ByteArray, sequence: Int): Boolean
    external fun lastError(): String
    external fun close(handle: Long)
}
