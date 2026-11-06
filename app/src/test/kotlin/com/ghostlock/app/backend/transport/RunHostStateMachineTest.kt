package com.ghostlock.app.backend.transport

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** Method-by-state cases for the one-shot RunHost lifecycle. */
class RunHostStateMachineTest {
    private val identity = RunHostIdentity("0".repeat(32), 1L)
    private val fingerprint = "a".repeat(64)

    /** Normal lifecycle reaches DESTROYED only through explicit close. */
    @Test
    fun prepareStartTerminalCloseIsLinear() {
        val machine = RunHostStateMachine()
        assertEquals(RunHostCode.OK, machine.setCallback(identity))
        assertEquals(RunHostCode.OK, machine.prepare(identity, fingerprint))
        assertEquals(RunHostCode.OK, machine.start(identity))
        assertEquals(false, machine.claimTerminal(identity))
        assertEquals(RunHostCode.OK, machine.requestClose(identity))
        assertTrue(machine.frameworkDestroyAuthorized())
        assertEquals(RunHostState.DESTROYED, machine.state())
    }

    /** Running work rejects direct close and an unsafe cancellation. */
    @Test
    fun runningUnsafeTransitionsDoNotMutateState() {
        val machine = RunHostStateMachine()
        machine.setCallback(identity)
        machine.prepare(identity, fingerprint)
        machine.start(identity)
        assertEquals(RunHostCode.UNSAFE_TO_CANCEL, machine.cancel(identity, false))
        assertEquals(RunHostCode.UNSAFE_TO_DESTROY, machine.requestClose(identity))
        assertEquals(RunHostState.RUNNING, machine.state())
        assertFalse(machine.frameworkDestroyAuthorized())
    }

    /** A safe running cancel reserves cancellation but keeps resources live until worker publication. */
    @Test
    fun runningSafeCancelWaitsForWorkerTerminal() {
        val machine = RunHostStateMachine()
        machine.setCallback(identity)
        machine.prepare(identity, fingerprint)
        machine.start(identity)
        assertEquals(RunHostCode.OK, machine.cancel(identity, true))
        assertEquals(RunHostState.RUNNING, machine.state())
        assertEquals(RunHostCode.UNSAFE_TO_DESTROY, machine.requestClose(identity))
        assertEquals(true, machine.claimTerminal(identity))
        assertEquals(RunHostState.TERMINAL, machine.state())
    }

    /** Exact identity and fingerprint retries remain idempotent before start. */
    @Test
    fun exactPrepareRetryIsIdempotent() {
        val machine = RunHostStateMachine()
        assertEquals(RunHostCode.OK, machine.setCallback(identity))
        assertEquals(RunHostCode.OK, machine.preparePreflight(identity, fingerprint))
        assertEquals(RunHostCode.OK, machine.prepare(identity, fingerprint))
        assertEquals(RunHostCode.ALREADY_APPLIED, machine.preparePreflight(identity, fingerprint))
        assertEquals(RunHostCode.ALREADY_APPLIED, machine.prepare(identity, fingerprint))
    }

    /** Exact mismatch codes are stable after an identity has been reserved. */
    @Test
    fun mismatchCodesRemainDistinct() {
        val machine = RunHostStateMachine()
        val current = RunHostIdentity("0".repeat(32), 2L)
        machine.setCallback(current)
        assertEquals(RunHostCode.STALE_GENERATION, machine.snapshotCode(current.copy(generation = 1L)))
        assertEquals(RunHostCode.BUSY, machine.snapshotCode(current.copy(generation = 3L)))
        assertEquals(RunHostCode.RUN_MISMATCH, machine.snapshotCode(current.copy(runId = "1".repeat(32))))
    }

    /** Destroyed state wins over a later identity mismatch for every method except idempotent close. */
    @Test
    fun destroyedStateHasPriorityOverIdentityMismatch() {
        val machine = RunHostStateMachine()
        machine.setCallback(identity)
        machine.requestClose(identity)
        val other = RunHostIdentity("1".repeat(32), 2L)
        assertEquals(RunHostCode.BAD_STATE, machine.setCallback(other))
        assertEquals(RunHostCode.BAD_STATE, machine.preparePreflight(other, fingerprint))
        assertEquals(RunHostCode.BAD_STATE, machine.start(other))
        assertEquals(RunHostCode.BAD_STATE, machine.snapshotCode(other))
        assertEquals(RunHostCode.BAD_STATE, machine.cancel(other, true))
        assertEquals(RunHostCode.ALREADY_APPLIED, machine.requestClose(other))
    }
}
