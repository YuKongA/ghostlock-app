package com.ghostlock.app.core.engine

import com.ghostlock.app.core.model.Plan
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

data class BackendRunRequest(
    val backendId: String,
    val pathId: String,
    val values: Map<String, Any>,
    val profile: ByteArray? = null,
    val expectedUid: Long? = null,
)

sealed interface BackendRunResult {
    val steps: List<NodeOutcome>
    data class Ok(val note: String, val uid: Long?, override val steps: List<NodeOutcome>) : BackendRunResult
    data class Failed(val reason: String, override val steps: List<NodeOutcome>, val uid: Long? = null) : BackendRunResult
}

fun interface Progress { fun onStep(name: String, index: Int, total: Int) }

/** One-shot backend execution boundary. It owns its nodes/ports and closes them once. */
interface BackendRun : AutoCloseable {
    val backendId: String
    suspend fun run(request: BackendRunRequest, progress: Progress = Progress { _, _, _ -> }): BackendRunResult
    fun stop()
    override fun close()
    suspend fun shutdown()
}

interface BackendRunFactory {
    val backendId: String
    fun create(): BackendRun
}

/** UI-facing one-shot engine over an injected BackendRun. */
class DelegatingExecutionEngine(
    private val run: BackendRun,
    private val log: (String) -> Unit = {},
) : ExecutionEngine {
    private val gate = Any()
    private var closed = false
    private var used = false
    private var running = false
    private val _state = MutableStateFlow<ExecutionState>(ExecutionState.Idle)
    override val state: StateFlow<ExecutionState> = _state.asStateFlow()

    override suspend fun run(plan: Plan) {
        execute(
            BackendRunRequest(
                backendId = run.backendId,
                pathId = plan.path.id,
                values = plan.values,
                profile = null,
                expectedUid = null,
            ),
        )
    }

    suspend fun execute(request: BackendRunRequest): BackendRunResult {
        synchronized(gate) {
            check(!closed) { "engine closed" }
            check(!used) { "engine already used" }
            check(!running) { "engine busy" }
            used = true
            running = true
        }
        _state.value = ExecutionState.Checking(run.backendId)
        _state.value = ExecutionState.RunningPath(request.pathId, 0f)
        return try {
            val result = run.run(request) { name, index, total ->
                _state.value = ExecutionState.RunningPath(name, (index + 1f) / total)
            }
            _state.value = ExecutionState.Finishing(request.pathId)
            _state.value = when (result) {
                is BackendRunResult.Ok -> ExecutionState.Success(result.note)
                is BackendRunResult.Failed -> ExecutionState.Failed(result.reason)
            }
            result
        } catch (cancelled: CancellationException) {
            run.stop()
            _state.value = ExecutionState.Failed("cancelled")
            throw cancelled
        } catch (problem: Throwable) {
            _state.value = ExecutionState.Failed(problem.message ?: problem.javaClass.simpleName)
            throw problem
        } finally {
            synchronized(gate) { running = false }
        }
    }

    override fun close() {
        synchronized(gate) {
            if (closed) return
            check(!running) { "engine busy" }
            closed = true
        }
        run.close()
        _state.value = ExecutionState.Failed("engine-closed")
    }
}

class ExecutionEngineFactory(private val factories: Map<String, BackendRunFactory>) {
    init {
        require(factories.all { (key, factory) -> key == factory.backendId })
        require(factories.keys.distinct().size == factories.size)
    }

    fun create(backendId: String): ExecutionEngine? {
        val factory = factories[backendId] ?: return null
        require(factory.backendId == backendId)
        return DelegatingExecutionEngine(factory.create())
    }
}
