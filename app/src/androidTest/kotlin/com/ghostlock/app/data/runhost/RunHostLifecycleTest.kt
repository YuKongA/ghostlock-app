package com.ghostlock.app.data.runhost

import androidx.test.ext.junit.runners.AndroidJUnit4
import com.ghostlock.app.core.runhost.RunHostCode
import com.ghostlock.app.core.runhost.RunHostIdentity
import com.ghostlock.app.core.runhost.RunHostStateMachine
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/** Explicit requestClose and framework-destroy authorization contract. */
@RunWith(AndroidJUnit4::class)
class RunHostLifecycleTest {
    /** Running hosts reject direct close; terminal hosts authorize exactly one destroy. */
    @Test
    fun destroyRequiresExplicitAcceptedClose() {
        val identity = RunHostIdentity("0".repeat(32), 1L)
        val machine = RunHostStateMachine()
        machine.setCallback(identity)
        machine.prepare(identity, "a".repeat(64))
        machine.start(identity)
        assertEquals(RunHostCode.UNSAFE_TO_DESTROY, machine.requestClose(identity))
        assertFalse(machine.frameworkDestroyAuthorized())
        assertEquals(false, machine.claimTerminal(identity))
        assertEquals(RunHostCode.OK, machine.requestClose(identity))
        assertTrue(machine.frameworkDestroyAuthorized())
    }
}
