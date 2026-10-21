package com.ghostlock.app

import androidx.test.ext.junit.runners.AndroidJUnit4
import com.ghostlock.app.backend.cve43499.ktstep.run.Cve43499ProductionSession
import com.ghostlock.app.backend.cve43499.ktstep.run.ProductionSessionResult
import com.ghostlock.app.engine.ktstep.NativePrepareParams
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/** AVD resource-lifecycle gate for the production session; no route/uid claim. */
@RunWith(AndroidJUnit4::class)
class Cve43499ProductionSessionInstrumentedTest {
    @Test
    fun opensAndClosesProductionSession() {
        val result = Cve43499ProductionSession.open(
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
        assertTrue(result is ProductionSessionResult.Ok)
        (result as ProductionSessionResult.Ok).session.close()
    }
}
