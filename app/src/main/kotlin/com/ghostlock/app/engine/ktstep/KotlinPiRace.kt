package com.ghostlock.app.engine.ktstep

/** 一个可做 PI futex 操作的字；真实实现会桥到 KernelSyscalls，测试用 fake。 */
interface FutexWord {
    /** 该字的原生地址；真实实现由 KernelSyscalls 的 mmap 给出。 */
    val address: Long

    fun lockPi(): Int
    fun waitRequeuePi(target: FutexWord): Int
    fun cmpRequeuePi(target: FutexWord): Int
    fun unlockPi(): Int

    /** 把字清 0（requeue 前对齐 val3=0）；失败返回 -errno。 */
    fun clearValue(): Int
}

enum class PiOutcome { NotPrepared, Prepared, Running, Disarmed, Refused }

/**
 * C++ `race::PiRace` 的 Kotlin 移植：只做一次 PI 尝试的决策，不做线程/页/路由。
 *
 * 顺序与 C++ 一致：target.lockPi → chain.cmpRequeuePi(target) 必须 ≥1 → Running；
 * disarm 只在 Running 时 unlock target。waitRequeuePi 由 waiter 线程调用，本类不调用。
 */
class KotlinPiRace(
    private val chain: FutexWord?,
    private val target: FutexWord?,
) {
    var outcome: PiOutcome = if (chain != null && target != null) PiOutcome.Prepared else PiOutcome.NotPrepared
        private set

    fun run(): PiOutcome {
        if (outcome != PiOutcome.Prepared) return outcome
        if (target == null || target.lockPi() != 0) {
            outcome = PiOutcome.Refused
            return outcome
        }
        if (chain == null || chain.cmpRequeuePi(target) < 1) {
            outcome = PiOutcome.Refused
            return outcome
        }
        outcome = PiOutcome.Running
        return outcome
    }

    fun disarm() {
        if (outcome == PiOutcome.Running) target?.unlockPi()
        outcome = PiOutcome.Disarmed
    }
}
