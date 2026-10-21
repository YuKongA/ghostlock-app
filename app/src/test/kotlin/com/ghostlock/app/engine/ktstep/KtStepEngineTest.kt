package com.ghostlock.app.engine.ktstep

import com.ghostlock.app.core.steps.StepId
import com.ghostlock.app.core.steps.StepOutcome
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class KtStepEngineTest {
    private fun pageInputs(): PayloadPageInputs = PayloadPageInputs(
        slots = PayloadSlots(0x0E80, 0x1180, 0x1280, 0x1080, 0x0F80),
        lock = LockFields(0, 8, 16, 24),
        waiter = WaiterFields(0, 8, 16, 0x18, 0x20, 0x28, 0x30, 0x38, 0x40, 0x44, 0x48, 0x50),
        task = TaskFields(0x40, 0x84, 0x8C, 0x90C, 0x920, 0x930, 0x938, 0x348),
        values = PayloadValues(0x100, 120, 140),
        waiterWord = 0xFFFF800000100000uL.toLong() + 0x1180L,
        waiterTask = 0L,
        fakeTask = 0xFFFF800000100000uL.toLong() + 0x1280L,
        fakeLock = 0xFFFF800000100000uL.toLong() + 0x0E80L,
        taskGroup = 0L,
        piTopTask = 0L,
        pointerBytes = 8,
    )

    private fun layoutInputs(): LayoutInputs = LayoutInputs(
        pageValue = 0x10000010001L,
        credentialValue = 0xFFFFFF802ABFD588uL.toLong(),
        defaultFops = 0x10L,
        credentialFops = 0x20L,
        pointerBytes = 8,
    )

    private fun steps(): Map<StepId, KtStep> = mapOf(
        StepId.W1 to KtStep(StepId.W1, 0x1000L, WriteMode.Zero, true),
        StepId.W2 to KtStep(StepId.W2, 0x2000L, WriteMode.Credential, true, layoutInputs().credentialValue),
        StepId.W3 to KtStep(StepId.W3, 0x3000L, WriteMode.Zero, false),
    )

    private fun plan(
        steps: Map<StepId, KtStep> = steps(),
        pageSize: Int = 0x8000,
        layoutInputs: LayoutInputs = layoutInputs(),
        attempts: Int = 1,
    ): KtStepPlan = KtStepPlan(
        steps = steps,
        layoutInputs = layoutInputs,
        pageInputs = pageInputs(),
        pageSize = pageSize,
        attempts = attempts,
    )

    @Test
    fun happyPathBuildsPagesAndLandsThreeSteps() {
        val route = FakeKernelRoute()
        val outcome = KtStepEngine(route, FakeVictimProbe()).run(plan())

        assertTrue(outcome.reason, outcome.fixtureSucceeded)
        assertFalse(outcome.succeeded)
        assertEquals(3, outcome.steps.size)
        assertTrue(outcome.steps.all { it.outcome == StepOutcome.Ran })
        assertEquals(3, route.requests.size)
        assertEquals(3, route.pages.size)
        assertEquals(layoutInputs().credentialValue, route.requests[1].value)
        assertEquals(WriteMode.Credential, route.requests[1].mode)
        assertEquals(3, outcome.traces.size)
        assertEquals(LayoutVerdict.Ok, outcome.traces[1].pageVerdict)
        assertEquals(layoutInputs().credentialValue, outcome.traces[1].layout.value)
        assertTrue(outcome.traces.all { it.page?.size == 0x8000 })
    }

    @Test
    fun aProductionRouteWithAFixtureProbeStaysFixture() {
        val route = object : KernelRoute {
            override val kind: RouteKind = RouteKind.Production
            override fun attempt(request: WriteRequest, page: ByteArray, inputs: PayloadPageInputs) =
                RouteResult.Landed()
        }
        val outcome = KtStepEngine(route, FakeVictimProbe()).run(plan())

        assertTrue(outcome.fixtureSucceeded)
        assertFalse(outcome.succeeded)
    }

    @Test
    fun layoutRefusalStopsBeforeTheRoute() {
        val route = FakeKernelRoute()
        val bad = layoutInputs().copy(pageValue = 0x10000000000L)
        val outcome = KtStepEngine(route, FakeVictimProbe()).run(plan(layoutInputs = bad))

        assertFalse(outcome.succeeded)
        assertEquals(0, route.requests.size)
        assertEquals("layout:w1", outcome.reason)
        assertEquals(1, outcome.traces.size)
    }

    @Test
    fun pageRefusalStopsBeforeTheRoute() {
        val route = FakeKernelRoute()
        val outcome = KtStepEngine(route, FakeVictimProbe()).run(plan(pageSize = 0x1000))

        assertFalse(outcome.succeeded)
        assertEquals(0, route.requests.size)
        assertEquals("page:NoRoom:w1", outcome.reason)
    }

    @Test
    fun routeRefusalAtW2StopsW3() {
        val route = FakeKernelRoute().apply {
            answer = { request ->
                if (request.target == 0x2000L) {
                    RouteResult.Refused("denied", retryable = false)
                } else {
                    RouteResult.Landed()
                }
            }
        }
        val outcome = KtStepEngine(route, FakeVictimProbe()).run(plan())

        assertFalse(outcome.succeeded)
        assertEquals(2, route.requests.size)
        assertEquals("route:denied", outcome.reason)
        assertEquals(2, outcome.traces.size)
    }

    @Test
    fun aRetryableRefusalUsesThePlanBudget() {
        val route = FakeKernelRoute().apply {
            answer = { RouteResult.Refused("not-ready", retryable = true) }
        }
        val outcome = KtStepEngine(route, FakeVictimProbe()).run(plan(attempts = 3))

        assertFalse(outcome.succeeded)
        assertEquals(3, route.requests.size)
        assertEquals("route:not-ready", outcome.reason)
    }

    @Test
    fun indeterminateFreezesTheEngine() {
        val route = FakeKernelRoute().apply {
            answer = { RouteResult.Indeterminate("in-flight") }
        }
        val outcome = KtStepEngine(route, FakeVictimProbe()).run(plan(attempts = 5))

        assertFalse(outcome.succeeded)
        assertEquals(1, route.requests.size)
        assertEquals(StepOutcome.Frozen, outcome.steps[0].outcome)
        assertEquals(StepOutcome.Frozen, outcome.steps[1].outcome)
    }

    @Test
    fun missingStepIsRejectedBeforeAnyRouteCall() {
        val route = FakeKernelRoute()
        val outcome = KtStepEngine(route, FakeVictimProbe()).run(plan(steps = steps().filterKeys { it != StepId.W3 }))

        assertFalse(outcome.succeeded)
        assertEquals("invalid:steps", outcome.reason)
        assertTrue(route.requests.isEmpty())
    }

    @Test
    fun aDiagnosticPiRouteFreezesBeforeTheVictimProbe() {
        val fake = FakeKernelSyscalls()
        fake.script("mmap", SyscallResult.Ok(0x1000L))
        fake.script("munmap", SyscallResult.Ok(0L))
        fake.futexByOp[SyscallFutexWord.FUTEX_CMP_REQUEUE_PI] = SyscallResult.Ok(1L)
        val probe = FakeVictimProbe()
        val outcome = KtStepEngine(PiActorsKernelRoute(fake), probe).run(plan())

        assertFalse(outcome.succeeded)
        assertTrue(outcome.steps.all { it.outcome == StepOutcome.Frozen })
        assertEquals(0, probe.calls)
    }

    @Test
    fun aNonZeroVictimUidFailsAfterTheSteps() {
        val route = FakeKernelRoute()
        val probe = FakeVictimProbe(SyscallResult.Ok(2000L))
        val outcome = KtStepEngine(route, probe).run(plan())

        assertFalse(outcome.succeeded)
        assertEquals("victim-uid:2000", outcome.reason)
        assertEquals(2000L, outcome.uid)
        assertEquals(3, route.requests.size)
        assertEquals(1, probe.calls)
    }

    @Test
    fun aProbeErrnoFailsAfterTheSteps() {
        val route = FakeKernelRoute()
        val probe = FakeVictimProbe(SyscallResult.Errno(KernelErrno.ENOSYS))
        val outcome = KtStepEngine(route, probe).run(plan())

        assertFalse(outcome.succeeded)
        assertEquals("probe:38", outcome.reason)
        assertEquals(3, route.requests.size)
    }

    @Test
    fun wrongStepShapeIsRejectedBeforeAnyRouteCall() {
        val route = FakeKernelRoute()
        val broken = steps().toMutableMap()
        broken[StepId.W2] = broken.getValue(StepId.W2).copy(value = 0x1L)
        val outcome = KtStepEngine(route, FakeVictimProbe()).run(plan(steps = broken))

        assertFalse(outcome.succeeded)
        assertEquals("invalid:step-shape:w2", outcome.reason)
        assertTrue(route.requests.isEmpty())
    }

    @Test
    fun duplicateTargetIsRejectedBeforeAnyRouteCall() {
        val route = FakeKernelRoute()
        val broken = steps().toMutableMap()
        broken[StepId.W2] = broken.getValue(StepId.W2).copy(target = 0x1000L)
        val outcome = KtStepEngine(route, FakeVictimProbe()).run(plan(steps = broken))

        assertFalse(outcome.succeeded)
        assertEquals("invalid:target-duplicate", outcome.reason)
        assertTrue(route.requests.isEmpty())
    }
}
