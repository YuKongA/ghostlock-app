package com.ghostlock.app.engine.ktstep

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class KotlinPiActorsTest {
    private class Word(
        private val lock: Int = 0,
        private val cmp: Int = 1,
        private val waitSleepMs: Long = 0L,
    ) : FutexWord {
        override val address: Long = 0x1000L
        var locks = 0
        var waits = 0
        var cmps = 0
        var unlocks = 0

        override fun lockPi(): Int {
            ++locks
            return lock
        }

        override fun waitRequeuePi(target: FutexWord): Int {
            ++waits
            if (waitSleepMs > 0L) {
                try {
                    Thread.sleep(waitSleepMs)
                } catch (interrupted: InterruptedException) {
                    Thread.currentThread().interrupt()
                    return -4
                }
            }
            return 0
        }

        override fun cmpRequeuePi(target: FutexWord): Int {
            ++cmps
            return cmp
        }

        override fun unlockPi(): Int {
            ++unlocks
            return 0
        }

        override fun clearValue(): Int = 0
    }

    @Test
    fun happyPathRunsAllThreeActors() {
        val chain = Word()
        val target = Word()
        val result = KotlinPiActors(chain, target).run()

        assertTrue(result.completed)
        assertFalse(result.timedOut)
        assertEquals(0, result.waiterResult)
        assertEquals(0, result.ownerResult)
        assertEquals(1, result.requeueResult)
        assertEquals(1, chain.locks)
        assertEquals(1, chain.waits)
        assertEquals(1, chain.cmps)
        assertEquals(1, chain.unlocks)
        assertEquals(1, target.locks)
        assertEquals(1, target.unlocks)
    }

    @Test
    fun aSeparateWaitWordIsLockedInsteadOfTheChain() {
        val chain = Word()
        val target = Word()
        val wait = Word()
        val result = KotlinPiActors(chain, target, wait).run()

        assertTrue(result.completed)
        assertEquals(1, chain.locks)
        assertEquals(1, chain.unlocks)
        assertEquals(0, wait.locks)
        assertEquals(1, wait.waits)
        assertEquals(1, wait.cmps)
    }

    @Test
    fun zeroRequeueIsReportedButActorsComplete() {
        val result = KotlinPiActors(Word(cmp = 0), Word()).run()

        assertTrue(result.completed)
        assertEquals(0, result.requeueResult)
    }

    @Test
    fun aFailedOwnerLockDoesNotUnlock() {
        val target = Word(lock = -1)
        val result = KotlinPiActors(Word(), target).run()

        assertTrue(result.completed)
        assertEquals(-1, result.ownerResult)
        assertEquals(1, target.locks)
        assertEquals(0, target.unlocks)
    }

    @Test
    fun aStuckWaiterTimesOutAndInterrupts() {
        val chain = Word(waitSleepMs = 500L)
        val target = Word()
        val result = KotlinPiActors(
            chain, target, config = PiActorsConfig(parkTimeoutNs = 1_000_000L, joinTimeoutMs = 30L),
        ).run()

        assertFalse(result.completed)
        assertTrue(result.timedOut)
    }
}
