package com.ghostlock.app.engine.ktstep

import org.junit.Assert.assertEquals
import org.junit.Test

class KotlinPiRaceTest {
    private class Word(
        var lockResult: Int = 0,
        var cmpResult: Int = 1,
        override val address: Long = 0L,
    ) : FutexWord {
        var locks = 0
        var cmps = 0
        var unlocks = 0

        override fun lockPi(): Int {
            ++locks
            return lockResult
        }

        override fun waitRequeuePi(target: FutexWord): Int = 0

        override fun cmpRequeuePi(target: FutexWord): Int {
            ++cmps
            return cmpResult
        }

        override fun unlockPi(): Int {
            ++unlocks
            return 0
        }

        override fun clearValue(): Int = 0
    }

    @Test
    fun aMovedWaiterIsRunningAndDisarmUnlocks() {
        val chain = Word()
        val target = Word()
        val race = KotlinPiRace(chain, target)

        assertEquals(PiOutcome.Running, race.run())
        assertEquals(1, target.locks)
        assertEquals(1, chain.cmps)
        race.disarm()
        assertEquals(1, target.unlocks)
        assertEquals(PiOutcome.Disarmed, race.outcome)
    }

    @Test
    fun aFailedTargetLockIsRefusedWithoutRequeue() {
        val chain = Word()
        val target = Word(lockResult = -1)
        val race = KotlinPiRace(chain, target)

        assertEquals(PiOutcome.Refused, race.run())
        assertEquals(1, target.locks)
        assertEquals(0, chain.cmps)
        assertEquals(0, target.unlocks)
    }

    @Test
    fun zeroMovedWaitersIsRefused() {
        val chain = Word(cmpResult = 0)
        val target = Word()
        val race = KotlinPiRace(chain, target)

        assertEquals(PiOutcome.Refused, race.run())
        assertEquals(1, chain.cmps)
        assertEquals(0, target.unlocks)
    }

    @Test
    fun aMissingWordIsNotPrepared() {
        val race = KotlinPiRace(null, Word())

        assertEquals(PiOutcome.NotPrepared, race.run())
    }
}
