package com.ghostlock.app.engine.ktstep

import com.ghostlock.app.core.steps.StepId
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class KtVictimFlowTest {
    private class Session(
        private val startResult: SyscallResult = SyscallResult.Ok(0L),
        private val uidResult: SyscallResult = SyscallResult.Ok(0L),
    ) : VictimSession {
        override val kind: ProbeKind = ProbeKind.Production
        var starts = 0
        var probes = 0
        var closes = 0

        override fun start(): SyscallResult {
            ++starts
            return startResult
        }

        override fun uid(): SyscallResult {
            ++probes
            return uidResult
        }

        override fun close() {
            ++closes
        }
    }

    private fun pageInputs(): PayloadPageInputs = PayloadPageInputs(
        slots = PayloadSlots(0x0E80, 0x1180, 0x1280, 0x1080, 0x0F80),
        lock = LockFields(0, 8, 16, 24),
        waiter = WaiterFields(0, 8, 16, 0x18, 0x20, 0x28, 0x30, 0x38, 0x40, 0x44, 0x48, 0x50),
        task = TaskFields(0x40, 0x84, 0x8C, 0x90C, 0x920, 0x930, 0x938, 0x348),
        values = PayloadValues(0x100, 120, 140),
        waiterWord = 0x1000L,
        waiterTask = 0L,
        fakeTask = 0x2000L,
        fakeLock = 0x3000L,
        taskGroup = 0L,
        piTopTask = 0L,
        pointerBytes = 8,
    )

    private fun plan(): KtStepPlan {
        val layout = LayoutInputs(0x10000010001L, 0xCAFEL, 0x10L, 0x20L, 8)
        return KtStepPlan(
            steps = mapOf(
                StepId.W1 to KtStep(StepId.W1, 0x1000L, WriteMode.Zero, true),
                StepId.W2 to KtStep(StepId.W2, 0x2000L, WriteMode.Credential, true, layout.credentialValue),
                StepId.W3 to KtStep(StepId.W3, 0x3000L, WriteMode.Zero, false),
            ),
            layoutInputs = layout,
            pageInputs = pageInputs(),
            pageSize = 0x8000,
        )
    }

    @Test
    fun aSuccessfulFixtureRunStillClosesTheVictim() {
        val session = Session()
        val route = FakeKernelRoute()
        val outcome = KtVictimFlow(route, session).run(plan())

        assertTrue(outcome.fixtureSucceeded)
        assertFalse(outcome.succeeded)
        assertEquals(1, session.starts)
        assertEquals(1, session.probes)
        assertEquals(1, session.closes)
        assertEquals(3, route.requests.size)
    }

    @Test
    fun aStartFailureSkipsTheRouteAndCloses() {
        val session = Session(startResult = SyscallResult.Errno(KernelErrno.ENOSYS))
        val route = FakeKernelRoute()
        val outcome = KtVictimFlow(route, session).run(plan())

        assertFalse(outcome.succeeded)
        assertEquals("victim-start:38", outcome.reason)
        assertEquals(0, route.requests.size)
        assertEquals(1, session.closes)
    }

    @Test
    fun aRouteFailureStillClosesTheVictim() {
        val session = Session()
        val route = FakeKernelRoute().apply {
            answer = { RouteResult.Refused("denied") }
        }
        val outcome = KtVictimFlow(route, session).run(plan())

        assertFalse(outcome.succeeded)
        assertEquals(1, session.closes)
    }

    @Test
    fun theProductionRouteCannotSucceedWhileTheWriterIsMissing() {
        val session = Session()
        val outcome = KtVictimFlow(KernelPageRoute(KernelPageWriter()), session).run(plan())

        assertFalse(outcome.succeeded)
        assertFalse(outcome.fixtureSucceeded)
        assertEquals("route:page:38", outcome.reason)
        assertEquals(1, session.closes)
    }
}
