package com.ghostlock.app.engine.ktstep

import java.nio.ByteBuffer
import java.nio.ByteOrder

data class NativePrepareParams(
    val pageSize: Long,
    val mmStructSize: Long,
    val mmSlabOrder: Long,
    val collisions: Int,
    val threadCount: Int,
    val possibleCpus: Int,
    val proofCount: Int,
    val proofSeed: Long,
    val rangeStart: Long,
    val rangeEnd: Long,
    val counterOffset: Int,
    val wakeRounds: Int,
    val roundTimeoutUs: Long,
    val headerBytes: Int,
    val totalPokes: Int,
    val wordBytes: Int,
    val handleBytes: Int,
    val preGroups: Int,
    val preShapedGroups: Int,
    val sprayGroups: Int,
    val postGroups: Int,
    val leakGroups: Int,
    val snitchCollisions: Int,
    val maxAttempts: Int,
    val pollRounds: Int,
    val socketBuffer: Int,
    val mainCpu: Int,
    val sweepTags: Boolean,
)

sealed interface NativePrepareResult {
    data class Ok(
        val ran: Boolean,
        val status: Int,
        val refusal: Int,
        val atStep: Int,
        val base: Long,
        val wordsBase: Long,
        val payloadBase: Long,
        val shapingFd: Int,
        val shapingPeerFd: Int,
        val opened: Long,
        val closed: Long,
        val error: Int,
        val proofCount: Long,
        val snitchFound: Long,
        val snitchAddress: Long,
        val snitchVerdict: Int,
        val proofs: LongArray,
        val words: LongArray,
        val payloadOffset: Long,
    ) : NativePrepareResult

    data class Errno(val code: Int) : NativePrepareResult
    data class Malformed(val reason: String) : NativePrepareResult
}

object NativePrepareCodec {
    const val MAGIC = 0x474C5050
    const val RESULT_MAGIC = 0x474C5052
    const val VERSION = 1
    const val HEADER_BYTES = 192
    const val RESULT_BYTES = 160

    sealed interface Encoded { data class Ok(val bytes: ByteArray) : Encoded; data class Invalid(val reason: String) : Encoded }

    fun encode(params: NativePrepareParams): Encoded {
        if (params.pageSize <= 0L || params.mmStructSize <= 0L || params.mmSlabOrder !in 0L..62L) {
            return Encoded.Invalid("geometry")
        }
        if (params.collisions !in 2..64 || params.threadCount <= 0 || params.possibleCpus <= 0 ||
            params.proofCount !in 1..4
        ) return Encoded.Invalid("counts")
        if (params.rangeStart == 0L || params.rangeEnd <= params.rangeStart) return Encoded.Invalid("range")
        if (params.counterOffset <= 0 || params.wakeRounds <= 0 || params.headerBytes <= 0 ||
            params.totalPokes <= 0 || params.wordBytes <= 0 || params.handleBytes <= 0 ||
            params.snitchCollisions < 2 || params.maxAttempts <= 0 || params.pollRounds <= 0
        ) return Encoded.Invalid("region")

        val bytes = ByteArray(HEADER_BYTES)
        val b = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
        b.putInt(0, MAGIC)
        b.putShort(4, VERSION.toShort())
        b.putShort(6, HEADER_BYTES.toShort())
        b.putInt(8, HEADER_BYTES)
        b.putLong(16, params.pageSize)
        b.putLong(24, params.mmStructSize)
        b.putLong(32, params.mmSlabOrder)
        b.putInt(40, params.collisions)
        b.putInt(44, params.threadCount)
        b.putInt(48, params.possibleCpus)
        b.putInt(52, params.proofCount)
        b.putLong(56, params.proofSeed)
        b.putLong(64, params.rangeStart)
        b.putLong(72, params.rangeEnd)
        b.putInt(80, params.counterOffset)
        b.putInt(84, params.wakeRounds)
        b.putLong(88, params.roundTimeoutUs)
        b.putInt(96, params.headerBytes)
        b.putInt(100, params.totalPokes)
        b.putInt(104, params.wordBytes)
        b.putInt(108, params.handleBytes)
        b.putInt(112, params.preGroups)
        b.putInt(116, params.preShapedGroups)
        b.putInt(120, params.sprayGroups)
        b.putInt(124, params.postGroups)
        b.putInt(128, params.leakGroups)
        b.putInt(132, params.snitchCollisions)
        b.putInt(136, params.maxAttempts)
        b.putInt(140, params.pollRounds)
        b.putInt(144, params.socketBuffer)
        b.putInt(148, params.mainCpu)
        b.put(152, if (params.sweepTags) 1 else 0)
        b.put(153, 0)
        return Encoded.Ok(bytes)
    }

    fun decodeResult(bytes: ByteArray): NativePrepareResult {
        if (bytes.size != RESULT_BYTES) return NativePrepareResult.Malformed("size")
        val b = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
        if (b.getInt(0) != RESULT_MAGIC) return NativePrepareResult.Malformed("magic")
        if (b.getShort(4).toInt() != VERSION) return NativePrepareResult.Malformed("version")
        return NativePrepareResult.Ok(
            ran = (b.get(12).toInt() and 0xFF) != 0,
            status = b.get(13).toInt() and 0xFF,
            refusal = b.get(14).toInt() and 0xFF,
            atStep = b.get(15).toInt() and 0xFF,
            base = b.getLong(16),
            wordsBase = b.getLong(24),
            payloadBase = b.getLong(32),
            shapingFd = b.getInt(40),
            shapingPeerFd = b.getInt(44),
            opened = b.getInt(48).toLong() and 0xFFFFFFFFL,
            closed = b.getInt(52).toLong() and 0xFFFFFFFFL,
            error = b.getInt(56),
            proofCount = b.getInt(60).toLong() and 0xFFFFFFFFL,
            snitchFound = b.getLong(64),
            snitchAddress = b.getLong(72),
            snitchVerdict = b.get(80).toInt() and 0xFF,
            proofs = LongArray(4) { b.getLong(88 + it * 8) },
            words = LongArray(4) { b.getLong(120 + it * 8) },
            payloadOffset = b.getInt(152).toLong() and 0xFFFFFFFFL,
        )
    }
}

object NativePrepareJni {
    external fun create(): Long
    external fun run(handle: Long, params: ByteArray): ByteArray?
    external fun word(handle: Long, index: Int): Long
    external fun wordsBase(handle: Long): Long
    external fun payloadBase(handle: Long): Long
    external fun close(handle: Long): Long
}

class NativePrepare43499Port {
    @Volatile
    private var handle: Long = 0L

    @Synchronized
    fun open(): SyscallResult {
        if (!NativeAttemptJni.ensureLoaded()) return SyscallResult.Errno(KernelErrno.ENOSYS)
        if (handle != 0L) return SyscallResult.Ok(handle)
        val created = NativePrepareJni.create()
        if (created == 0L) return SyscallResult.Errno(KernelErrno.ENOSYS)
        handle = created
        return SyscallResult.Ok(created)
    }

    fun prepare(params: NativePrepareParams): NativePrepareResult {
        val current = handle
        if (current == 0L) return NativePrepareResult.Errno(KernelErrno.ENOSYS)
        val encoded = when (val packed = NativePrepareCodec.encode(params)) {
            is NativePrepareCodec.Encoded.Ok -> packed.bytes
            is NativePrepareCodec.Encoded.Invalid -> return NativePrepareResult.Malformed(packed.reason)
        }
        val answer = NativePrepareJni.run(current, encoded) ?: return NativePrepareResult.Malformed("null")
        return NativePrepareCodec.decodeResult(answer)
    }

    fun word(index: Int): Long = NativePrepareJni.word(handle, index)
    fun wordsBase(): Long = NativePrepareJni.wordsBase(handle)
    fun payloadBase(): Long = NativePrepareJni.payloadBase(handle)

    @Synchronized
    fun close() {
        val current = handle
        handle = 0L
        if (current != 0L) NativePrepareJni.close(current)
    }
}
