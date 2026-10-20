package com.ghostlock.app.engine.pagecache.stage

import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class StageRunnerTest {
    private class Fake(
        private val name: String,
        private val result: StageResult,
        private val closed: MutableList<String>,
        private val aborted: MutableList<String>,
    ) : Stage {
        override suspend fun run(): StageResult = result
        override suspend fun abort(code: StageCode) { aborted += name }
        override suspend fun close() { closed += name }
    }

    @Test
    fun stopsOnFailureAndClosesAllInReverse() = runBlocking {
        val closed = mutableListOf<String>()
        val aborted = mutableListOf<String>()
        val stages = listOf(
            Fake("env", StageResult(StageCode.OK), closed, aborted),
            Fake("hook", StageResult(StageCode.HOOK_PARTIAL, "bad"), closed, aborted),
            Fake("cleanup", StageResult(StageCode.OK), closed, aborted),
        )
        val results = StageRunner(stages).run()
        assertEquals(2, results.size)
        assertEquals(StageCode.HOOK_PARTIAL, results.last().code)
        assertEquals(listOf("hook"), aborted)
        assertEquals(listOf("cleanup", "hook", "env"), closed)
    }

    @Test
    fun cleanupIsIdempotent() = runBlocking {
        var calls = 0
        val cleanup = CleanupStage { calls += 1; true }
        assertTrue(cleanup.run().code == StageCode.OK)
        assertTrue(cleanup.run().code == StageCode.OK)
        assertEquals(1, calls)
    }
}
