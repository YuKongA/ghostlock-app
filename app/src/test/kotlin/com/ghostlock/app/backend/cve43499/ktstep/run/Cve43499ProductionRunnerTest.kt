package com.ghostlock.app.backend.cve43499.ktstep.run

import com.ghostlock.app.engine.ktstep.KtEnginePhase
import com.ghostlock.app.engine.ktstep.KtStepPlan
import com.ghostlock.app.engine.ktstep.LayoutInputs
import com.ghostlock.app.engine.ktstep.LockFields
import com.ghostlock.app.engine.ktstep.NativePrepareParams
import com.ghostlock.app.engine.ktstep.PayloadPageInputs
import com.ghostlock.app.engine.ktstep.PayloadSlots
import com.ghostlock.app.engine.ktstep.PayloadValues
import com.ghostlock.app.engine.ktstep.TaskFields
import com.ghostlock.app.engine.ktstep.WaiterFields
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class Cve43499ProductionRunnerTest {
    private fun params() = NativePrepareParams(
        pageSize = 4096, mmStructSize = 1024, mmSlabOrder = 2, collisions = 4,
        threadCount = 4, possibleCpus = 8, proofCount = 4, proofSeed = 0,
        rangeStart = 0, rangeEnd = 0, counterOffset = 0, wakeRounds = 0,
        roundTimeoutUs = 0, headerBytes = 0, totalPokes = 0, wordBytes = 8,
        handleBytes = 8, preGroups = 1, preShapedGroups = 0, sprayGroups = 0,
        postGroups = 0, leakGroups = 0, snitchCollisions = 0, maxAttempts = 0,
        pollRounds = 0, socketBuffer = 0, mainCpu = 0, sweepTags = false,
    )

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

    @Test
    fun nativeOpenFailureIsNormalized() = runBlocking {
        val outcome = Cve43499ProductionRunner.run(params(), plan())
        assertEquals(KtEnginePhase.Failed, outcome.phase)
        assertTrue(outcome.reason!!.startsWith("session:"))
        assertTrue(outcome.steps.isEmpty())
    }
}
