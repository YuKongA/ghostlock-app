package com.ghostlock.app

import androidx.test.ext.junit.runners.AndroidJUnit4
import com.ghostlock.app.engine.ktstep.ProcessVictim
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNotNull
import org.junit.Test
import org.junit.runner.RunWith

/** N5 baseline：victim child 可启动/可回报 uid；未接页时不要求 root。 */
@RunWith(AndroidJUnit4::class)
class VictimBaselineInstrumentedTest {
    @Test
    fun theVictimReportsItsUidBeforeAnyLanding() {
        ProcessVictim().use { victim ->
            val uid = victim.uid()
            assertNotNull(uid)
            assertNotEquals(0, uid)
        }
    }
}
