package com.ghostlock.app.backend.cve43284.pagecache

import com.ghostlock.app.core.engine.Overall
import com.ghostlock.app.engine.pagecache.stage.Stage
import com.ghostlock.app.engine.pagecache.stage.StageCode
import com.ghostlock.app.engine.pagecache.stage.StageResult
import com.ghostlock.app.engine.pagecache.stage.StageStages
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class Cve43284RunTest {
    private class FakeStage(private val result: StageResult) : Stage {
        var closes = 0; private set
        override suspend fun run(): StageResult = result
        override fun abort(code: StageCode) {}
        override fun close() { closes += 1 }
    }

    private fun stage(code: StageCode = StageCode.OK, reason: String = "") =
        FakeStage(StageResult(code, reason))

    @Test
    fun allStagesAndCleanupRunInOrder() = runBlocking {
        val nodes = List(6) { stage() }
        val cleanup = stage()
        val result = Cve43284Run(StageStages(nodes, cleanup)).run()
        assertEquals(Overall.Ok, result.overall)
        assertEquals(listOf("environment", "helper", "module", "hook", "trigger", "wait", "cleanup"),
            result.outcomes.map { it.node.key })
        assertEquals(1, cleanup.closes)
    }

    @Test
    fun partialStopsAndCleanupStillRuns() = runBlocking {
        val nodes = List(6) { stage() }.toMutableList()
        nodes[1] = stage(StageCode.MODULE_PARTIAL, "partial")
        val cleanup = stage()
        val result = Cve43284Run(StageStages(nodes, cleanup)).run()
        assertEquals(Overall.Failed, result.overall)
        assertEquals(3, result.outcomes.size)
        assertTrue(result.primary is com.ghostlock.app.core.engine.RunNodeResult.Frozen)
    }

    @Test
    fun cleanupFailureOverridesSuccess() = runBlocking {
        val nodes = List(6) { stage() }
        val cleanup = stage(StageCode.RESTORE_FAILED, "restore")
        val result = Cve43284Run(StageStages(nodes, cleanup)).run()
        assertEquals(Overall.Failed, result.overall)
        assertTrue(result.primary is com.ghostlock.app.core.engine.RunNodeResult.Frozen)
    }
}
