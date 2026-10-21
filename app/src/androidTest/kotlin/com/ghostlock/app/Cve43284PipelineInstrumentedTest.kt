package com.ghostlock.app

import androidx.test.ext.junit.runners.AndroidJUnit4
import com.ghostlock.app.backend.cve43284.pagecache.Cve43284Run
import com.ghostlock.app.core.engine.Overall
import com.ghostlock.app.engine.pagecache.stage.Stage
import com.ghostlock.app.engine.pagecache.stage.StageCode
import com.ghostlock.app.engine.pagecache.stage.StageResult
import com.ghostlock.app.engine.pagecache.stage.StageStages
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith

/** AVD 43284 Kotlin pipeline parity: fake stages, no native call. */
@RunWith(AndroidJUnit4::class)
class Cve43284PipelineInstrumentedTest {
    private class FakeStage : Stage {
        override suspend fun run(): StageResult = StageResult(StageCode.OK)
        override fun abort(code: StageCode) = Unit
        override fun close() = Unit
    }

    @Test
    fun kotlinStagePipelineParityOnDevice() = runBlocking {
        val stages = StageStages(List(6) { FakeStage() }, FakeStage())
        val result = Cve43284Run(stages).run()
        assertEquals(Overall.Ok, result.overall)
        assertEquals(7, result.outcomes.size)
    }
}
