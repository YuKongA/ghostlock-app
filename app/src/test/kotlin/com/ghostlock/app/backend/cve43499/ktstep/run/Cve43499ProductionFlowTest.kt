package com.ghostlock.app.backend.cve43499.ktstep.run

import com.ghostlock.app.engine.ktstep.KtEnginePhase
import com.ghostlock.app.engine.ktstep.KtStepOutcome
import com.ghostlock.app.engine.ktstep.KtStepPlan
import com.ghostlock.app.engine.ktstep.LayoutInputs
import com.ghostlock.app.engine.ktstep.LockFields
import com.ghostlock.app.engine.ktstep.PayloadPageInputs
import com.ghostlock.app.engine.ktstep.PayloadSlots
import com.ghostlock.app.engine.ktstep.PayloadValues
import com.ghostlock.app.engine.ktstep.ProbeKind
import com.ghostlock.app.engine.ktstep.SyscallResult
import com.ghostlock.app.engine.ktstep.TaskFields
import com.ghostlock.app.engine.ktstep.VictimSession
import com.ghostlock.app.engine.ktstep.VictimStartResult
import com.ghostlock.app.engine.ktstep.WaiterFields
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class Cve43499ProductionFlowTest {
    private class FakeVictim(private val start: VictimStartResult = VictimStartResult.Ok) : VictimSession {
        var closes = 0; private set
        override val kind = ProbeKind.Fixture
        override fun start(): SyscallResult = SyscallResult.Ok(0)
        override fun startDetailed(): VictimStartResult = start
        override fun uid(): SyscallResult = SyscallResult.Ok(0)
        override fun close() { closes += 1 }
    }

    private fun plan() = KtStepPlan(
        steps = emptyMap(),
        layoutInputs = LayoutInputs(1, 2, 0, 0, 8),
        pageInputs = PayloadPageInputs(
            PayloadSlots(0,0,0,0,0), LockFields(0,0,0,0),
            WaiterFields(0,0,0,0,0,0,0,0,0,0,0,0), TaskFields(0,0,0,0,0,0,0,0),
            PayloadValues(0,0,0), 0, 0, 0, 0, 0, 0, 8,
        ),
        pageSize = 1,
    )

    private fun ok() = KtStepOutcome(KtEnginePhase.FixtureSucceeded, null, emptyList(), emptyList(), 0)

    @Test
    fun successClosesVictimOnce() = runBlocking {
        val victim = FakeVictim()
        val flow = Cve43499ProductionFlow({ victim }) { _, _ -> ok() }
        assertEquals(KtEnginePhase.FixtureSucceeded, flow.run(plan()).phase)
        assertEquals(1, victim.closes)
    }

    @Test
    fun startFailureReturnsNamedOutcomeAndCloses() = runBlocking {
        val victim = FakeVictim(VictimStartResult.Failed("victim-start:7"))
        val flow = Cve43499ProductionFlow({ victim }) { _, _ -> error("must not run") }
        val outcome = flow.run(plan())
        assertEquals(KtEnginePhase.Failed, outcome.phase)
        assertEquals("victim-start:7", outcome.reason)
        assertTrue(outcome.steps.isEmpty())
        assertEquals(1, victim.closes)
    }

    @Test
    fun factoryFailureIsNamed() = runBlocking {
        val flow = Cve43499ProductionFlow({ error("no-process") }) { _, _ -> ok() }
        assertEquals("victim-start:create", flow.run(plan()).reason)
    }

    @Test
    fun cancellationClosesAndPreservesIdentity() = runBlocking {
        val victim = FakeVictim()
        val cancel = CancellationException("stop")
        val flow = Cve43499ProductionFlow({ victim }) { _, _ -> throw cancel }
        var seen: CancellationException? = null
        try { flow.run(plan()) } catch (c: CancellationException) { seen = c }
        assertTrue(seen === cancel)
        assertEquals(1, victim.closes)
    }
}
