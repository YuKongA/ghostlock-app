package com.ghostlock.app.backend.cve43284.pagecache

import com.ghostlock.app.core.engine.RunNodeResult
import com.ghostlock.app.engine.pagecache.stage.Evidence
import com.ghostlock.app.engine.pagecache.stage.Stage
import com.ghostlock.app.engine.pagecache.stage.StageCode
import com.ghostlock.app.engine.pagecache.stage.StageResult
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class Cve43284StageNodeTest {
    private class FakeStage(private val result: StageResult) : Stage {
        var aborts = 0; private set
        var closes = 0; private set
        override suspend fun run(): StageResult = result
        override fun abort(code: StageCode) { aborts += 1 }
        override fun close() { closes += 1 }
    }

    @Test
    fun mapsStageCodesAndForwardsCleanup() = runBlocking {
        val ok = FakeStage(StageResult(StageCode.OK, evidence = Evidence(written = 2, source = "x")))
        val done = Cve43284StageNode(ok, "helper", "helper").run()
        assertTrue(done is RunNodeResult.Done)
        assertEquals("2", (done as RunNodeResult.Done).evidence["written"])

        val partial = FakeStage(StageResult(StageCode.MODULE_PARTIAL, "partial"))
        assertTrue(Cve43284StageNode(partial, "module", "module").run() is RunNodeResult.Frozen)

        val cancelled = FakeStage(StageResult(StageCode.CANCELLED, "stop"))
        assertTrue(Cve43284StageNode(cancelled, "wait", "wait").run() is RunNodeResult.Frozen)

        val node = Cve43284StageNode(ok, "helper", "helper")
        node.abort("cancelled"); node.close()
        assertEquals(1, ok.aborts); assertEquals(1, ok.closes)
    }
}
