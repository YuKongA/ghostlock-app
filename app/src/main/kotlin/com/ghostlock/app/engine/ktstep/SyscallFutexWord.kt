package com.ghostlock.app.engine.ktstep

/** 真实 FutexWord：把四个 PI 操作桥到 KernelSyscalls.futex；不持页/不选地址。 */
class SyscallFutexWord(
    private val syscalls: KernelSyscalls,
    override val address: Long,
    private val pid: Int = 0,
    private val waitTimeoutNs: Long = 200_000_000L,
) : FutexWord {
    override fun lockPi(): Int = raw(
        syscalls.futex(address, FUTEX_LOCK_PI, 0, -1L, 0L, 0),
    )

    override fun waitRequeuePi(target: FutexWord): Int = raw(
        /* wait 是普通 futex（初始 0）；等待条件必须与该字一致。 */
        syscalls.futex(address, FUTEX_WAIT_REQUEUE_PI, 0, waitTimeoutNs, target.address, 0),
    )

    override fun cmpRequeuePi(target: FutexWord): Int = raw(
        /* C++ 约定 val=1；chain 字需在 requeue 前预置 1（当前 Kotlin 尚无写回原语）。 */
        syscalls.futex(address, FUTEX_CMP_REQUEUE_PI, 1, -1L, target.address, 0),
    )

    override fun unlockPi(): Int = raw(
        syscalls.futex(address, FUTEX_UNLOCK_PI, 0, -1L, 0L, 0),
    )

    override fun clearValue(): Int = when (val result = syscalls.processVmWritev(
        pid, address, byteArrayOf(0, 0, 0, 0),
    )) {
        is SyscallResult.Ok -> 0
        is SyscallResult.Errno -> -result.code
    }

    private fun raw(result: SyscallResult): Int = when (result) {
        is SyscallResult.Ok -> result.value.toInt()
        is SyscallResult.Errno -> -result.code
    }

    companion object {
        const val FUTEX_LOCK_PI = 6
        const val FUTEX_UNLOCK_PI = 7
        const val FUTEX_WAIT_REQUEUE_PI = 11
        const val FUTEX_CMP_REQUEUE_PI = 12
    }
}
