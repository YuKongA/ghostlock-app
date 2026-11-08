package com.ghostlock.app.core.runhost

import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicReference
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
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

    /** A start that observes PREPARED blocks on the host lock until the complete candidate is published. */
    @Test
    fun hostLockPreventsStartFromObservingUnpublishedCandidate() {
        listOf("app", "shizuku").forEach { host ->
            val machine = RunHostStateMachine()
            val hostLock = Any()
            val candidate = Any()
            val prepared = AtomicReference<Any?>(null)
            val prepareCommitted = CountDownLatch(1)
            val startCommitted = CountDownLatch(1)
            val allowPublication = CountDownLatch(1)
            val observed = AtomicReference<Any?>(null)
            val prepareResult = AtomicReference<RunHostCode>()
            val startResult = AtomicReference<RunHostCode>()

            val prepareThread = Thread({
                synchronized(hostLock) {
                    val code = machine.prepare(identity, fingerprint)
                    prepareResult.set(code)
                    prepareCommitted.countDown()
                    check(allowPublication.await(5L, TimeUnit.SECONDS))
                    if (code == RunHostCode.OK) prepared.set(candidate)
                }
            }, "prepare-$host")
            val startThread = Thread({
                check(prepareCommitted.await(5L, TimeUnit.SECONDS))
                startResult.set(machine.start(identity))
                startCommitted.countDown()
                synchronized(hostLock) { observed.set(prepared.get()) }
            }, "start-$host")

            prepareThread.start()
            startThread.start()
            assertTrue("$host start did not observe PREPARED", startCommitted.await(5L, TimeUnit.SECONDS))
            assertEquals(RunHostState.RUNNING, machine.state())
            assertTrue("$host start must wait for candidate publication", startThread.isAlive)
            allowPublication.countDown()
            prepareThread.join(5_000L)
            startThread.join(5_000L)

            assertFalse("$host prepare thread remained live", prepareThread.isAlive)
            assertFalse("$host start thread remained live", startThread.isAlive)
            assertEquals(RunHostCode.OK, prepareResult.get())
            assertEquals(RunHostCode.OK, startResult.get())
            assertNotNull("$host start observed an empty prepared slot", observed.get())
            assertTrue(observed.get() === candidate)
        }
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
