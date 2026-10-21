package com.ghostlock.app.core.engine

import com.ghostlock.app.core.model.DeviceInfo
import com.ghostlock.app.core.model.ExecPath
import com.ghostlock.app.core.model.Finisher
import com.ghostlock.app.core.model.PathOutput
import com.ghostlock.app.core.model.Plan
import com.ghostlock.app.core.model.Privilege
import com.ghostlock.app.core.model.RunContext
import com.ghostlock.app.core.model.Support
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class BackendRunEngineTest {
    private class Output(override val pathId: String) : PathOutput
    private val path = object : ExecPath {
        override val id = "p"; override val title = "p"; override val privilege = Privilege.NONE
        override fun probe(device: DeviceInfo): Support = Support.Supported()
        override suspend fun run(context: RunContext): PathOutput = Output(id)
    }
    private val finisher = object : Finisher {
        override val id = "f"; override val title = "f"
        override suspend fun finish(output: PathOutput, context: RunContext) {}
    }
    private fun plan() = Plan(path, finisher)

    private class FakeRun(
        override val backendId: String = "fake",
        private val result: BackendRunResult = BackendRunResult.Ok("ok", 0, emptyList()),
        private val cancel: CancellationException? = null,
    ) : BackendRun {
        var stops = 0; private set
        var closes = 0; private set
        override suspend fun run(request: BackendRunRequest, progress: Progress): BackendRunResult {
            cancel?.let { throw it }
            return result
        }
        override fun stop() { stops += 1 }
        override fun close() { closes += 1 }
        override suspend fun shutdown() = Unit
    }

    @Test
    fun successAndFailureStates() = runBlocking {
        val ok = FakeRun()
        val engine = DelegatingExecutionEngine(ok)
        engine.run(plan())
        assertTrue(engine.state.value is ExecutionState.Success)
        engine.close(); engine.close()
        assertEquals(1, ok.closes)

        val failed = FakeRun(result = BackendRunResult.Failed("no", emptyList()))
        val engine2 = DelegatingExecutionEngine(failed)
        engine2.run(plan())
        assertTrue(engine2.state.value is ExecutionState.Failed)
    }

    @Test
    fun cancellationStopsThenRethrows() = runBlocking {
        val cancel = CancellationException("stop")
        val run = FakeRun(cancel = cancel)
        val engine = DelegatingExecutionEngine(run)
        var seen: CancellationException? = null
        try { engine.run(plan()) } catch (c: CancellationException) { seen = c }
        assertTrue(seen === cancel)
        assertEquals(1, run.stops)
    }

    @Test(expected = IllegalStateException::class)
    fun secondRunRejected() = runBlocking {
        val engine = DelegatingExecutionEngine(FakeRun())
        engine.run(plan()); engine.run(plan())
    }

    @Test
    fun factoryRejectsKeyMismatch() {
        val good = object : BackendRunFactory {
            override val backendId = "a"; override fun create() = FakeRun(backendId = "a")
        }
        assertNotNull(ExecutionEngineFactory(mapOf("a" to good)).create("a"))
        assertNull(ExecutionEngineFactory(mapOf("a" to good)).create("b"))
        val bad = object : BackendRunFactory {
            override val backendId = "b"; override fun create() = FakeRun(backendId = "b")
        }
        try { ExecutionEngineFactory(mapOf("a" to bad)); assertTrue(false) } catch (_: IllegalArgumentException) { }
    }
}
