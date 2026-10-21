package com.ghostlock.app

import androidx.test.ext.junit.runners.AndroidJUnit4
import com.ghostlock.app.core.steps.StepId
import com.ghostlock.app.engine.ktstep.KernelRoute
import com.ghostlock.app.engine.ktstep.KtEnginePhase
import com.ghostlock.app.engine.ktstep.KtStep
import com.ghostlock.app.engine.ktstep.KtStepEngine
import com.ghostlock.app.engine.ktstep.KtStepPlan
import com.ghostlock.app.engine.ktstep.LayoutInputs
import com.ghostlock.app.engine.ktstep.LockFields
import com.ghostlock.app.engine.ktstep.PayloadPageInputs
import com.ghostlock.app.engine.ktstep.PayloadSlots
import com.ghostlock.app.engine.ktstep.PayloadValues
import com.ghostlock.app.engine.ktstep.RouteResult
import com.ghostlock.app.engine.ktstep.SyscallResult
import com.ghostlock.app.engine.ktstep.TaskFields
import com.ghostlock.app.engine.ktstep.VictimProbe
import com.ghostlock.app.engine.ktstep.WaiterFields
import com.ghostlock.app.engine.ktstep.WriteMode
import com.ghostlock.app.engine.ktstep.WriteRequest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/** AVD Kotlin execution parity: no native call, fake route/probe only. */
@RunWith(AndroidJUnit4::class)
class KtStepPipelineInstrumentedTest {
    private fun plan(): KtStepPlan {
        val layout = LayoutInputs(0x10000010001L, 0xFFFFFF802ABFD588uL.toLong(), 0x10L, 0x20L, 8)
        val page = PayloadPageInputs(
            slots = PayloadSlots(0x0E80, 0x1180, 0x1280, 0x1080, 0x0F80),
            lock = LockFields(0, 8, 16, 24),
            waiter = WaiterFields(0, 8, 16, 0x18, 0x20, 0x28, 0x30, 0x38, 0x40, 0x44, 0x48, 0x50),
            task = TaskFields(0x40, 0x84, 0x8C, 0x90C, 0x920, 0x930, 0x938, 0x348),
            values = PayloadValues(0x100, 120, 140),
            waiterWord = 0xFFFF800000100000uL.toLong() + 0x1180L, waiterTask = 0L,
            fakeTask = 0xFFFF800000100000uL.toLong() + 0x1280L,
            fakeLock = 0xFFFF800000100000uL.toLong() + 0x0E80L,
            taskGroup = 0L, piTopTask = 0L, pointerBytes = 8,
        )
        return KtStepPlan(
            steps = mapOf(
                StepId.W1 to KtStep(StepId.W1, 0x1000L, WriteMode.Zero, true),
                StepId.W2 to KtStep(StepId.W2, 0x2000L, WriteMode.Credential, true, layout.credentialValue),
                StepId.W3 to KtStep(StepId.W3, 0x3000L, WriteMode.Zero, false),
            ),
            layoutInputs = layout, pageInputs = page, pageSize = 0x8000,
        )
    }

    @Test
    fun kotlinPipelineParityOnDevice() {
        val route = object : KernelRoute {
            var calls = 0
            override fun attempt(request: WriteRequest, page: ByteArray, inputs: PayloadPageInputs): RouteResult {
                calls += 1
                return RouteResult.Landed()
            }
        }
        val probe = object : VictimProbe {
            override fun uid(): SyscallResult = SyscallResult.Ok(0)
        }
        val outcome = KtStepEngine(route, probe).run(plan())

        assertEquals(KtEnginePhase.FixtureSucceeded, outcome.phase)
        assertEquals(3, outcome.steps.size)
        assertEquals(3, route.calls)
        assertTrue(outcome.steps.all { it.outcome == com.ghostlock.app.core.steps.StepOutcome.Ran })
    }
}
