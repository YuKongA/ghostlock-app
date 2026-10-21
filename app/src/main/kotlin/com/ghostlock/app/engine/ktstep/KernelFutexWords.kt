package com.ghostlock.app.engine.ktstep

/** 一页里放的两个 PI 字：chain 在前，target 在后（各 8 字节对齐）。 */
sealed interface FutexWordsResult {
    data class Ok(val words: KernelFutexWords) : FutexWordsResult
    data class Errno(val code: Int) : FutexWordsResult
}

class KernelFutexWords private constructor(
    private val syscalls: KernelSyscalls,
    val mapping: Long,
    private val length: Long,
    private val pid: Int,
    private val waitTimeoutNs: Long,
) {
    val chain = SyscallFutexWord(syscalls, mapping, pid, waitTimeoutNs)
    val target = SyscallFutexWord(syscalls, mapping + 8L, pid, waitTimeoutNs)
    val wait = SyscallFutexWord(syscalls, mapping + 16L, pid, waitTimeoutNs)

    /** 读回 chain 字；诊断用，失败返回 null。 */
    fun readChain(): Int? = readWord(0L)

    fun readTarget(): Int? = readWord(8L)

    fun readWait(): Int? = readWord(16L)

    private fun readWord(offset: Long): Int? {
        val bytes = ByteArray(4)
        val read = syscalls.processVmReadv(pid, mapping + offset, bytes)
        if (read !is SyscallResult.Ok || read.value != 4L) return null
        return (bytes[0].toInt() and 0xFF) or
            ((bytes[1].toInt() and 0xFF) shl 8) or
            ((bytes[2].toInt() and 0xFF) shl 16) or
            ((bytes[3].toInt() and 0xFF) shl 24)
    }

    /** 释放映射；重复 close 由调用方避免，失败返回 errno。 */
    fun close(): SyscallResult = syscalls.munmap(mapping, length)

    companion object {
        const val PROT_READ = 1
        const val PROT_WRITE = 2
        const val MAP_PRIVATE = 0x02
        const val MAP_ANONYMOUS = 0x20

        /** @return 一页匿名映射上的三个字，或具名 errno。 */
        fun create(
            syscalls: KernelSyscalls,
            length: Long = 4096L,
            initialChainValue: Int = 0,
            initialWaitValue: Int = 0,
            waitTimeoutNs: Long = -1L,
        ): FutexWordsResult {
            if (length <= 0L) return FutexWordsResult.Errno(KernelErrno.EINVAL)
            val mapping = when (val answer = syscalls.mmap(
                length, PROT_READ or PROT_WRITE, MAP_PRIVATE or MAP_ANONYMOUS, -1, 0L,
            )) {
                is SyscallResult.Ok -> {
                    if (answer.value <= 0L) return FutexWordsResult.Errno(KernelErrno.EINVAL)
                    answer.value
                }
                is SyscallResult.Errno -> return FutexWordsResult.Errno(answer.code)
            }
            val pid = when (val answer = syscalls.getpid()) {
                is SyscallResult.Ok -> answer.value.toInt()
                is SyscallResult.Errno -> {
                    syscalls.munmap(mapping, length)
                    return FutexWordsResult.Errno(answer.code)
                }
            }
            val word = byteArrayOf(
                (initialChainValue and 0xFF).toByte(),
                ((initialChainValue ushr 8) and 0xFF).toByte(),
                ((initialChainValue ushr 16) and 0xFF).toByte(),
                ((initialChainValue ushr 24) and 0xFF).toByte(),
            )
            val wroteChain = syscalls.processVmWritev(pid, mapping, word)
            val waitWord = word.copyOf().also {
                it[0] = (initialWaitValue and 0xFF).toByte()
                it[1] = ((initialWaitValue ushr 8) and 0xFF).toByte()
                it[2] = ((initialWaitValue ushr 16) and 0xFF).toByte()
                it[3] = ((initialWaitValue ushr 24) and 0xFF).toByte()
            }
            val wroteWait = syscalls.processVmWritev(pid, mapping + 16L, waitWord)
            if (wroteChain !is SyscallResult.Ok || wroteChain.value != 4L ||
                wroteWait !is SyscallResult.Ok || wroteWait.value != 4L
            ) {
                syscalls.munmap(mapping, length)
                return FutexWordsResult.Errno(KernelErrno.EINVAL)
            }
            return FutexWordsResult.Ok(KernelFutexWords(syscalls, mapping, length, pid, waitTimeoutNs))
        }
    }
}

/** 一次 PI 决策 + 映射生命周期的 spike；线程/页/路由不在本类。 */
data class PiSpikeResult(
    val outcome: PiOutcome?,
    val mapping: Long?,
    val close: SyscallResult?,
    val refusal: String?,
)

class PiSpike(private val syscalls: KernelSyscalls) {
    fun run(): PiSpikeResult {
        val made = when (val result = KernelFutexWords.create(syscalls)) {
            is FutexWordsResult.Errno -> return PiSpikeResult(null, null, null, "mmap:" + result.code)
            is FutexWordsResult.Ok -> result.words
        }
        val race = KotlinPiRace(made.chain, made.target)
        val outcome = race.run()
        race.disarm()
        val close = made.close()
        return PiSpikeResult(outcome, made.mapping, close, null)
    }
}
