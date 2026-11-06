package com.ghostlock.app.backend.transport

import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith

/** Typed abort-policy state transitions independent of process killing. */
@RunWith(AndroidJUnit4::class)
class RunHostCancellationTest {
    /** Safe running cancellation keeps resources live until worker terminal publication. */
    @Test
    fun safeCancellationTerminatesOnce() {
        val identity = RunHostIdentity("0".repeat(32), 1L)
        val machine = RunHostStateMachine()
        machine.setCallback(identity)
        machine.prepare(identity, "a".repeat(64))
        machine.start(identity)
        assertEquals(RunHostCode.OK, machine.cancel(identity, true))
        assertEquals(RunHostState.RUNNING, machine.state())
        assertEquals(RunHostCode.UNSAFE_TO_DESTROY, machine.requestClose(identity))
        assertEquals(true, machine.claimTerminal(identity))
        assertEquals(RunHostState.TERMINAL, machine.state())
        assertEquals(RunHostCode.ALREADY_APPLIED, machine.cancel(identity, true))
    }
}
