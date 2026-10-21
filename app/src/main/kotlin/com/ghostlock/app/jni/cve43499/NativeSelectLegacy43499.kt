package com.ghostlock.app.jni.cve43499

import java.nio.ByteBuffer
import java.nio.ByteOrder

data class NativeSelectLegacyResult(
    val status: Int,
    val step: Int,
    val errno: Int,
    val userspaceClean: Int,
    val kernelDisarmed: Int,
) {
    val ok: Boolean get() = status == 0

    companion object {
        const val BYTES = 32
        fun decode(bytes: ByteArray?): NativeSelectLegacyResult? {
            if (bytes == null || bytes.size < BYTES) return null
            val b = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
            return NativeSelectLegacyResult(
                status = b.getInt(0), step = b.getInt(4), errno = b.getInt(8),
                userspaceClean = b.getInt(12), kernelDisarmed = b.getInt(16),
            )
        }
    }
}

object NativeSelectLegacyJni {
    init { System.loadLibrary("gl43499native") }
    private external fun execute(target: Long, mode: Int, preserveChild: Int, fake: LongArray): ByteArray?
    fun run(
        target: Long,
        mode: Int,
        preserveChild: Boolean,
        fake: LongArray = LongArray(0),
    ): NativeSelectLegacyResult? =
        NativeSelectLegacyResult.decode(execute(target, mode, if (preserveChild) 1 else 0, fake))
}
