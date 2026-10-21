package com.ghostlock.app.engine.ktstep

import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

data class PiActorsConfig(
    val parkTimeoutNs: Long = 200_000_000L,
    val joinTimeoutMs: Long = 2_000L,
    val requeueSettleMs: Long = 0L,
)

data class PiActorsResult(
    val waiterResult: Int,
    val waitResult: Int,
    val ownerResult: Int,
    val requeueResult: Int,
    val completed: Boolean,
    val timedOut: Boolean,
)

/**
 * 三演员的最小 Kotlin 骨架：waiter(chain lock/park) · owner(target lock/requeue gate) · requeue(cmp)。
 *
 * 这是 K0b/K3 的线程编排骨架，不是已证明的内核竞争：真机读数仍是唯一判据。
 * 所有 futex op 走 FutexWord；线程只做顺序/等待，不读 profile、不选 route。
 */
class KotlinPiActors(
    private val chain: FutexWord,
    private val target: FutexWord,
    private val wait: FutexWord = chain,
    private val config: PiActorsConfig = PiActorsConfig(),
    private val phase: (String) -> Unit = {},
) {
    fun run(): PiActorsResult {
        val waiterWaiting = CountDownLatch(1)
        val ownerStarted = CountDownLatch(1)
        val requeueDone = CountDownLatch(1)
        var waiterResult = 0
        var waitResult = 0
        var ownerResult = 0
        var requeueResult = 0

        val waiter = Thread({
            var locked = false
            try {
                phase("waiter:lock-enter")
                waiterResult = chain.lockPi()
                phase("waiter:locked=" + waiterResult)
                locked = waiterResult == 0
                waiterWaiting.countDown()
                if (locked) {
                    phase("waiter:park-enter")
                    waitResult = wait.waitRequeuePi(target)
                    phase("waiter:park=" + waitResult)
                }
            } finally {
                if (locked) {
                    phase("waiter:unlock-enter")
                    chain.unlockPi()
                }
                phase("waiter:done")
            }
        }, "gl-pi-waiter")

        val owner = Thread({
            var locked = false
            try {
                phase("owner:await-waiter")
                waiterWaiting.await()
                phase("owner:lock-enter")
                ownerResult = target.lockPi()
                phase("owner:locked=" + ownerResult)
                locked = ownerResult == 0
                ownerStarted.countDown()
                phase("owner:await-requeue")
                requeueDone.await()
            } catch (interrupted: InterruptedException) {
                Thread.currentThread().interrupt()
            } finally {
                if (locked) {
                    phase("owner:unlock-enter")
                    target.unlockPi()
                }
                phase("owner:done")
            }
        }, "gl-pi-owner")

        val requeue = Thread({
            try {
                phase("requeue:await-owner")
                ownerStarted.await()
                phase("requeue:settle")
                if (config.requeueSettleMs > 0L) Thread.sleep(config.requeueSettleMs)
                phase("requeue:clear")
                target.clearValue()
                phase("requeue:cmp-enter")
                requeueResult = wait.cmpRequeuePi(target)
                phase("requeue:cmp=" + requeueResult)
            } catch (interrupted: InterruptedException) {
                Thread.currentThread().interrupt()
            } finally {
                requeueDone.countDown()
                phase("requeue:done")
            }
        }, "gl-pi-requeue")

        for (thread in listOf(waiter, owner, requeue)) {
            thread.isDaemon = true
            thread.start()
        }

        val deadline = System.nanoTime() + TimeUnit.MILLISECONDS.toNanos(config.joinTimeoutMs)
        fun join(thread: Thread): Boolean {
            val left = deadline - System.nanoTime()
            if (left <= 0L) return thread.isAlive
            return try {
                thread.join(TimeUnit.NANOSECONDS.toMillis(left).coerceAtLeast(1L))
                thread.isAlive
            } catch (interrupted: InterruptedException) {
                Thread.currentThread().interrupt()
                true
            }
        }

        val stillAlive = join(waiter) or join(owner) or join(requeue)
        if (stillAlive) {
            waiter.interrupt()
            owner.interrupt()
            requeue.interrupt()
        }
        return PiActorsResult(waiterResult, waitResult, ownerResult, requeueResult, !stillAlive, stillAlive)
    }
}
