package com.ghostlock.app.core.engine

import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class PipelineEngineTest {
    private class FakeNode(
        override val key: String,
        override val policy: RunPolicy = RunPolicy(),
        private val results: MutableList<RunNodeResult> = mutableListOf(RunNodeResult.Done()),
    ) : RunNode {
        override val backendId: String = "fake"
        override val token: String = key
        var aborts = 0; private set
        var closes = 0; private set
        var runs = 0; private set

        override suspend fun run(): RunNodeResult {
            runs += 1
            return if (results.size > 1) results.removeAt(0) else results.first()
        }

        override fun calls(): List<String> = List(runs) { key }
        override fun abort(reason: String) { aborts += 1 }
        override fun close() { closes += 1 }
    }

    @Test
    fun runsInOrderAndClosesReverseOnce() = runBlocking {
        val a = FakeNode("a"); val b = FakeNode("b")
        val engine = PipelineEngine()
        val result = engine.run(listOf(a, b))
        assertEquals(Overall.Ok, result.overall)
        assertEquals(listOf("a", "b"), result.outcomes.map { it.node.key })
        engine.close(); engine.close()
        assertEquals(1, b.closes); assertEquals(1, a.closes)
    }

    @Test
    fun retriesThenRefusesWithAttemptCalls() = runBlocking {
        val node = FakeNode("retry", RunPolicy(attempts = 3, settleMs = 0),
            mutableListOf(RunNodeResult.Retryable("not-ready"), RunNodeResult.Retryable("not-ready")))
        val result = PipelineEngine().run(listOf(node))
        assertTrue(result.primary is RunNodeResult.Refused)
        assertEquals(Overall.Failed, result.overall)
        assertEquals(3, result.outcomes.single().calls.size)
    }

    @Test
    fun frozenRemainingOnlyForFrozen() = runBlocking {
        val frozen = FakeNode("f", RunPolicy(skipPolicy = SkipPolicy.FrozenRemaining),
            mutableListOf(RunNodeResult.Frozen("x")))
        val tail = FakeNode("t")
        val result = PipelineEngine().run(listOf(frozen, tail))
        assertEquals(2, result.outcomes.size)
        assertTrue(result.outcomes[1].result is RunNodeResult.Frozen)

        val refused = FakeNode("r", RunPolicy(skipPolicy = SkipPolicy.FrozenRemaining),
            mutableListOf(RunNodeResult.Refused("no")))
        val tail2 = FakeNode("t2")
        assertEquals(1, PipelineEngine().run(listOf(refused, tail2)).outcomes.size)
    }

    @Test
    fun cleanupRunsAfterFailureAndOverrides() = runBlocking {
        val main = FakeNode("main", results = mutableListOf(RunNodeResult.Frozen("bad")))
        val cleanup = FakeNode("cleanup", results = mutableListOf(RunNodeResult.Frozen("restore")))
        val result = PipelineEngine().run(listOf(main), cleanup)
        assertEquals(2, result.outcomes.size)
        assertTrue(result.primary is RunNodeResult.Frozen)
        assertTrue(result.primary.toString().contains("cleanup"))
        assertEquals(Overall.Failed, result.overall)
    }

    @Test
    fun cancellationPreservesIdentityAndRunsCleanup() = runBlocking {
        val cancel = CancellationException("stop")
        val node = object : RunNode {
            override val backendId = "fake"; override val key = "c"; override val token = "c"
            override val policy = RunPolicy()
            override suspend fun run(): RunNodeResult = throw cancel
            override fun abort(reason: String) {}
            override fun close() {}
        }
        val cleanup = FakeNode("cleanup")
        var seen: CancellationException? = null
        try { PipelineEngine().run(listOf(node), cleanup) } catch (c: CancellationException) { seen = c }
        assertTrue(seen === cancel)
        assertEquals(1, cleanup.runs)
    }

    @Test
    fun thrownNodeBecomesFrozenAndAborts() = runBlocking {
        var aborts = 0
        val node = object : RunNode {
            override val backendId = "fake"; override val key = "x"; override val token = "x"
            override val policy = RunPolicy()
            override suspend fun run(): RunNodeResult = throw IllegalStateException("boom")
            override fun abort(reason: String) { aborts += 1 }
            override fun close() {}
        }
        val result = PipelineEngine().run(listOf(node))
        assertTrue(result.primary is RunNodeResult.Frozen)
        assertEquals(1, aborts)
    }

    @Test
    fun closeClosesCleanupOnce() = runBlocking {
        val node = FakeNode("n"); val cleanup = FakeNode("cleanup")
        val engine = PipelineEngine()
        engine.run(listOf(node), cleanup)
        engine.close(); engine.close()
        assertEquals(1, node.closes); assertEquals(1, cleanup.closes)
    }

    @Test(expected = IllegalStateException::class)
    fun runAfterCloseRejected() = runBlocking {
        val engine = PipelineEngine(); engine.close(); engine.run(listOf(FakeNode("a")))
        Unit
    }
}
