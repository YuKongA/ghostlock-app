package com.ghostlock.app.engine.kernelmemory

import com.ghostlock.app.core.steps.StepId
import com.ghostlock.app.core.steps.StepOutcome
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class KernelMemoryEngineTest {
    private fun request(
        steps: Map<StepId, KernelStep> = defaultSteps(),
        expectedUid: Long = 0L,
        finishAction: Int = 0,
        attempts: Int = 1,
    ): KernelMemoryRun = KernelMemoryRun(
        session = TestDtos.session(),
        prepare = TestDtos.prepare(),
        discover = { generation -> TestDtos.discover(generation) },
        steps = steps,
        expectedUid = expectedUid,
        finishAction = finishAction,
        attempts = attempts,
    )

    private fun defaultSteps(): Map<StepId, KernelStep> = mapOf(
        StepId.W1 to KernelStep(StepId.W1, route = 1, offset = 0L, address = 0x1000L, target = 0x1000L, value = 0L),
        StepId.W2 to KernelStep(StepId.W2, route = 1, offset = 0L, address = 0x2000L, target = 0x2000L, value = 0xCAFEL),
        StepId.W3 to KernelStep(StepId.W3, route = 1, offset = 0L, address = 0x3000L, target = 0x3000L, value = 0L),
    )

    @Test
    fun happyPathRunsThreeStepsAndVerifiesUid() = runBlocking {
        val port = FakeBackend43499Port()
        val outcome = KernelMemoryEngine(port).run(request())

        assertTrue(outcome.reason, outcome.succeeded)
        assertEquals(0L, outcome.uid)
        assertEquals(3, outcome.steps.size)
        assertTrue(outcome.steps.all { it.outcome == StepOutcome.Ran })
        assertEquals(listOf(StepId.W1, StepId.W2, StepId.W3), port.writes.map { it.step })
        assertEquals(listOf(1, 3, 2), port.writes.map { it.form })
        assertEquals(0xCAFEL, port.writes[1].value)
        assertEquals(
            listOf(
                "create", "initialize", "prepare", "discover:1", "spawn",
                "write:w1", "write:w2", "write:w3", "probe:0", "finish:0:1", "close",
            ),
            port.calls,
        )
    }

    @Test
    fun prepareRefusalStopsBeforeDiscoverAndStillCloses() = runBlocking {
        val port = FakeBackend43499Port().apply {
            prepareResult = PortResult.Refused(PortRefusal(PortCode.NOT_READY, "not-ready:prepare"))
        }
        val outcome = KernelMemoryEngine(port).run(request())

        assertFalse(outcome.succeeded)
        assertEquals("prepare:not-ready:not-ready:prepare", outcome.reason)
        assertEquals(listOf("create", "initialize", "prepare", "close"), port.calls)
    }

    @Test
    fun aRefusedDiscoverDoesNotSpawnOrWrite() = runBlocking {
        val port = FakeBackend43499Port().apply {
            discoverResult = PortResult.Refused(PortRefusal(PortCode.UNSUPPORTED, "generation-not-published"))
        }
        val outcome = KernelMemoryEngine(port).run(request())

        assertFalse(outcome.succeeded)
        assertEquals("discover:unsupported:generation-not-published", outcome.reason)
        assertFalse(port.calls.any { it.startsWith("spawn") || it.startsWith("write:") })
        assertEquals("close", port.calls.last())
    }

    @Test
    fun w2RefusalStopsW3AndStillFinishesVictim() = runBlocking {
        val port = FakeBackend43499Port().apply {
            writeHandler = { write ->
                if (write.step == StepId.W2) {
                    PortResult.Refused(PortRefusal(PortCode.FAILED, "write-refused"))
                } else {
                    PortResult.Ok(Unit)
                }
            }
        }
        val outcome = KernelMemoryEngine(port).run(request())

        assertFalse(outcome.succeeded)
        assertEquals("failed:write-refused", outcome.reason)
        assertEquals(listOf(StepId.W1, StepId.W2), port.writes.map { it.step })
        assertTrue(port.calls.contains("finish:0:1"))
        assertEquals("close", port.calls.last())
    }

    @Test
    fun indeterminateFreezesAndDoesNotRetry() = runBlocking {
        val port = FakeBackend43499Port().apply {
            writeHandler = { PortResult.Refused(PortRefusal(PortCode.IN_FLIGHT, "in-flight")) }
        }
        val outcome = KernelMemoryEngine(port).run(request(attempts = 5))

        assertFalse(outcome.succeeded)
        assertEquals(1, port.writes.size)
        assertEquals(3, outcome.steps.size)
        assertEquals(StepOutcome.Frozen, outcome.steps[0].outcome)
        assertEquals(StepOutcome.Frozen, outcome.steps[1].outcome)
        assertEquals(StepOutcome.Frozen, outcome.steps[2].outcome)
    }

    @Test
    fun wrongUidFailsAfterStepsAndFinishesVictim() = runBlocking {
        val port = FakeBackend43499Port().apply { probeHandler = { PortResult.Ok(2000L) } }
        val outcome = KernelMemoryEngine(port).run(request())

        assertFalse(outcome.succeeded)
        assertEquals("victim-uid:2000", outcome.reason)
        assertEquals(2000L, outcome.uid)
        assertTrue(port.calls.contains("finish:0:1"))
    }

    @Test
    fun cancellationAfterSpawnFinishesAndCloses() = runBlocking {
        val port = FakeBackend43499Port().apply {
            probeHandler = { throw CancellationException("stop") }
        }
        var cancelled = false
        try {
            KernelMemoryEngine(port).run(request())
        } catch (expected: CancellationException) {
            cancelled = true
        }

        assertTrue(cancelled)
        assertTrue(port.calls.contains("finish:0:1"))
        assertEquals("close", port.calls.last())
    }

    @Test
    fun missingStepsAreRejectedBeforeAnyNativeCall() = runBlocking {
        val port = FakeBackend43499Port()
        val broken = defaultSteps().filterKeys { it != StepId.W3 }
        val outcome = KernelMemoryEngine(port).run(request(steps = broken))

        assertFalse(outcome.succeeded)
        assertEquals("invalid:steps", outcome.reason)
        assertTrue(port.calls.isEmpty())
    }

    @Test
    fun duplicateAddressesAreRejectedBeforeAnyNativeCall() = runBlocking {
        val port = FakeBackend43499Port()
        val steps = defaultSteps().toMutableMap()
        steps[StepId.W2] = steps.getValue(StepId.W2).copy(address = 0x1000L, target = 0x1000L)
        val outcome = KernelMemoryEngine(port).run(request(steps = steps))

        assertFalse(outcome.succeeded)
        assertEquals("invalid:address-duplicate", outcome.reason)
        assertTrue(port.calls.isEmpty())
    }

    @Test
    fun targetMustEqualAddress() = runBlocking {
        val port = FakeBackend43499Port()
        val steps = defaultSteps().toMutableMap()
        steps[StepId.W1] = steps.getValue(StepId.W1).copy(target = 0x9999L)
        val outcome = KernelMemoryEngine(port).run(request(steps = steps))

        assertFalse(outcome.succeeded)
        assertEquals("invalid:target-address:w1", outcome.reason)
        assertTrue(port.calls.isEmpty())
    }

    @Test
    fun handoffIsRejectedInK1BeforeAnyNativeCall() = runBlocking {
        val port = FakeBackend43499Port()
        val outcome = KernelMemoryEngine(port).run(request(finishAction = 2))

        assertFalse(outcome.succeeded)
        assertEquals("invalid:finish-action", outcome.reason)
        assertTrue(port.calls.isEmpty())
    }

    @Test
    fun zeroTargetIsRejectedBeforeAnyNativeCall() = runBlocking {
        val port = FakeBackend43499Port()
        val steps = defaultSteps().toMutableMap()
        steps[StepId.W2] = steps.getValue(StepId.W2).copy(target = 0L)
        val outcome = KernelMemoryEngine(port).run(request(steps = steps))

        assertFalse(outcome.succeeded)
        assertEquals("invalid:target:w2", outcome.reason)
        assertTrue(port.calls.isEmpty())
    }

    @Test
    fun closeFailureIsReportedEvenAfterSuccess() = runBlocking {
        val port = FakeBackend43499Port().apply {
            closeResult = PortResult.Refused(PortRefusal(PortCode.FAILED, "close-refused"))
        }
        val outcome = KernelMemoryEngine(port).run(request())

        assertFalse(outcome.succeeded)
        assertEquals("close:failed:close-refused", outcome.reason)
    }
}
