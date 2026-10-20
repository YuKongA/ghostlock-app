package com.ghostlock.app.engine.pagecache.stage

import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Test

class StagePageCacheEngineTest {
    private class Fixed(private val result: StageResult) : Stage {
        override suspend fun run(): StageResult = result
        override suspend fun abort(code: StageCode) = Unit
        override suspend fun close() = Unit
    }

    @Test
    fun runsInOrderAndCleanupOnFailure() = runBlocking {
        var cleanupRuns = 0
        val ok = Fixed(StageResult(StageCode.OK))
        val bad = Fixed(StageResult(StageCode.HOOK_PARTIAL, "bad"))
        val cleanup = object : Stage {
            override suspend fun run(): StageResult { cleanupRuns += 1; return StageResult(StageCode.OK) }
            override suspend fun abort(code: StageCode) = Unit
            override suspend fun close() = Unit
        }
        val results = StagePageCacheEngine(ok, ok, ok, bad, ok, ok, cleanup).run()
        assertEquals(5, results.size)
        assertEquals(StageCode.HOOK_PARTIAL, results[3].code)
        assertEquals(StageCode.OK, results.last().code)
        assertEquals(1, cleanupRuns)
    }
}
