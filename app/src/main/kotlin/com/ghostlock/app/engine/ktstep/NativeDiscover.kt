package com.ghostlock.app.engine.ktstep

import java.nio.ByteBuffer
import java.nio.ByteOrder

data class NativeDiscoverParams(
    val wordsAddress: Long,
    val wordsBytes: Int,
    val collisions: Int,
    val threadCount: Int,
    val possibleCpus: Int,
    val pageSize: Long,
    val mmStructSize: Long,
    val mmSlabOrder: Long,
    val headerBytes: Int,
    val totalPokes: Int,
    val wordBytes: Int,
    val handleBytes: Int,
    val wakeRounds: Int,
    val roundTimeoutUs: Long,
    val rangeStart: Long,
    val rangeEnd: Long,
    val counterOffset: Int,
    val sweepTags: Boolean,
)

sealed interface NativeDiscoverResult {
    data class Ok(
        val ran: Boolean,
        val verdict: Int,
        val refusal: Int,
        val found: Long,
        val mmStruct: Long,
        val probes: Long,
        val rounds: Long,
        val pokes: Long,
        val error: Int,
        val wordsBase: Long,
        val wordsBytes: Long,
    ) : NativeDiscoverResult

    data class Errno(val code: Int) : NativeDiscoverResult
    data class Malformed(val reason: String) : NativeDiscoverResult
}

object NativeDiscoverCodec {
    const val MAGIC = 0x474C4450
    const val RESULT_MAGIC = 0x474C4452
    const val VERSION = 1
    const val HEADER_BYTES = 120
    const val RESULT_BYTES = 64

    sealed interface Encoded { data class Ok(val bytes: ByteArray) : Encoded; data class Invalid(val reason: String) : Encoded }

    fun encode(params: NativeDiscoverParams): Encoded {
        if (params.wordsAddress == 0L || params.wordsBytes <= 0) return Encoded.Invalid("words")
        if (params.collisions !in 1..64 || params.threadCount <= 0 || params.possibleCpus <= 0) {
            return Encoded.Invalid("counts")
        }
        if (params.pageSize <= 0L || params.mmStructSize <= 0L || params.mmSlabOrder <= 0L) {
            return Encoded.Invalid("geometry")
        }
        if (params.headerBytes <= 0 || params.totalPokes <= 0 || params.wordBytes <= 0 ||
            params.handleBytes <= 0 || params.wakeRounds <= 0
        ) return Encoded.Invalid("region")
        if (params.rangeStart == 0L || params.rangeEnd <= params.rangeStart) return Encoded.Invalid("range")
        val bytes = ByteArray(HEADER_BYTES)
        val b = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
        b.putInt(0, MAGIC)
        b.putShort(4, VERSION.toShort())
        b.putShort(6, HEADER_BYTES.toShort())
        b.putInt(8, HEADER_BYTES)
        b.putLong(16, params.wordsAddress)
        b.putInt(24, params.wordsBytes)
        b.putInt(28, params.collisions)
        b.putInt(32, params.threadCount)
        b.putInt(36, params.possibleCpus)
        b.putLong(40, params.pageSize)
        b.putLong(48, params.mmStructSize)
        b.putLong(56, params.mmSlabOrder)
        b.putInt(64, params.headerBytes)
        b.putInt(68, params.totalPokes)
        b.putInt(72, params.wordBytes)
        b.putInt(76, params.handleBytes)
        b.putInt(80, params.wakeRounds)
        b.putLong(84, params.roundTimeoutUs)
        b.putLong(92, params.rangeStart)
        b.putLong(100, params.rangeEnd)
        b.putInt(108, params.counterOffset)
        b.put(112, if (params.sweepTags) 1 else 0)
        b.put(113, 0)
        return Encoded.Ok(bytes)
    }

    fun decodeResult(bytes: ByteArray): NativeDiscoverResult {
        if (bytes.size != RESULT_BYTES) return NativeDiscoverResult.Malformed("size")
        val b = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
        if (b.getInt(0) != RESULT_MAGIC) return NativeDiscoverResult.Malformed("magic")
        if (b.getShort(4).toInt() != VERSION) return NativeDiscoverResult.Malformed("version")
        return NativeDiscoverResult.Ok(
            ran = (b.get(8).toInt() and 0xFF) != 0,
            verdict = b.get(9).toInt() and 0xFF,
            refusal = b.get(10).toInt() and 0xFF,
            found = b.getLong(16),
            mmStruct = b.getLong(24),
            probes = b.getInt(32).toLong() and 0xFFFFFFFFL,
            rounds = b.getInt(36).toLong() and 0xFFFFFFFFL,
            pokes = b.getInt(40).toLong() and 0xFFFFFFFFL,
            error = b.getInt(44),
            wordsBase = b.getLong(48),
            wordsBytes = b.getInt(56).toLong() and 0xFFFFFFFFL,
        )
    }
}

object NativeDiscoverJni {
    external fun discover(handle: Long, params: ByteArray): ByteArray?
}

class NativeDiscover43499Port {
    @Volatile
    private var handle: Long = 0L

    @Synchronized
    fun open(): SyscallResult {
        if (!NativeAttemptJni.ensureLoaded()) return SyscallResult.Errno(KernelErrno.ENOSYS)
        if (handle != 0L) return SyscallResult.Ok(handle)
        val created = NativeAttemptJni.create(0)
        if (created <= 0L) return SyscallResult.Errno(KernelErrno.ENOSYS)
        handle = created
        return SyscallResult.Ok(created)
    }

    fun discover(params: NativeDiscoverParams): NativeDiscoverResult {
        val current = handle
        if (current == 0L) return NativeDiscoverResult.Errno(KernelErrno.ENOSYS)
        val encoded = when (val packed = NativeDiscoverCodec.encode(params)) {
            is NativeDiscoverCodec.Encoded.Ok -> packed.bytes
            is NativeDiscoverCodec.Encoded.Invalid -> return NativeDiscoverResult.Malformed(packed.reason)
        }
        val answer = NativeDiscoverJni.discover(current, encoded)
            ?: return NativeDiscoverResult.Malformed("null")
        return NativeDiscoverCodec.decodeResult(answer)
    }

    @Synchronized
    fun close() {
        val current = handle
        handle = 0L
        if (current != 0L) NativeAttemptJni.close(current)
    }
}
