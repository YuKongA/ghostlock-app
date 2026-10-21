package com.ghostlock.app.core.engine

import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.withContext

/** Per-node retry/skip policy. 43284 uses attempts=1/None; 43499 uses per-step attempts. */
data class RunPolicy(
    val attempts: Int = 1,
    val settleMs: Long = 0L,
    val skipPolicy: SkipPolicy = SkipPolicy.None,
) {
    init {
        require(attempts >= 1) { "attempts must be at least one" }
        require(settleMs >= 0L) { "settle must not be negative" }
    }
}

enum class SkipPolicy { None, FrozenRemaining }

sealed interface RunNodeResult {
    data class Done(val evidence: Map<String, String> = emptyMap()) : RunNodeResult
    data class Retryable(val reason: String) : RunNodeResult
    data class Refused(
        val reason: String,
        val evidence: Map<String, String> = emptyMap(),
    ) : RunNodeResult
    data class Frozen(
        val reason: String,
        val evidence: Map<String, String> = emptyMap(),
    ) : RunNodeResult
}

/** One execution unit. It owns no engine state; PipelineEngine owns order/close. */
interface RunNode {
    val backendId: String
    val key: String
    val token: String
    val policy: RunPolicy

    suspend fun run(): RunNodeResult
    fun calls(): List<String> = emptyList()
    fun abort(reason: String)
    fun close()
}

data class StepSpec(val backendId: String, val key: String, val token: String)

interface StepCatalog<I> {
    val backendId: String
    fun steps(): List<StepSpec>
    fun select(tokens: List<String>): Pair<List<StepSpec>, String?>
    fun plan(spec: StepSpec, inputs: I): com.ghostlock.app.core.steps.StepPlans.StepPlan
}

data class NodeDescriptor(val backendId: String, val key: String, val token: String)

data class NodeOutcome(
    val node: NodeDescriptor,
    val result: RunNodeResult,
    val calls: List<String> = emptyList(),
)

enum class Overall { Ok, Failed }

data class PipelineResult(
    val outcomes: List<NodeOutcome>,
    val primary: RunNodeResult?,
    val cleanup: NodeOutcome?,
    val overall: Overall,
)

/**
 * The single execution authority: order, retry, stop, freeze, cancellation, cleanup and close.
 *
 * Refused stops and emits no remaining nodes. Frozen stops and, when the failing node's policy is
 * FrozenRemaining, appends Frozen("engine-frozen") for every remaining node. Cleanup runs once in a
 * NonCancellable finally; cancellation is rethrown after cleanup. close() is reverse-order and
 * idempotent; run-after-close and concurrent run/close are rejected.
 */
class PipelineEngine(private val log: (String) -> Unit = {}) : AutoCloseable {
    private val gate = Any()
    private var running = false
    private var closed = false
    @Volatile private var current: RunNode? = null
    private var ownedNodes: List<RunNode> = emptyList()
    private var ownedCleanup: RunNode? = null

    suspend fun run(nodes: List<RunNode>, cleanup: RunNode? = null): PipelineResult {
        synchronized(gate) {
            check(!closed) { "pipeline closed" }
            check(!running) { "pipeline busy" }
            running = true
            ownedNodes = nodes
            ownedCleanup = cleanup
        }
        val outcomes = mutableListOf<NodeOutcome>()
        var primary: RunNodeResult? = null
        var stopPolicy = SkipPolicy.None
        try {
            for ((index, node) in nodes.withIndex()) {
                if (primary != null) {
                    if (primary is RunNodeResult.Frozen && stopPolicy == SkipPolicy.FrozenRemaining) {
                        outcomes += NodeOutcome(descriptor(node), RunNodeResult.Frozen("engine-frozen"))
                    }
                    continue
                }
                current = node
                log("node=" + node.token + " start")
                val result = execute(node)
                outcomes += NodeOutcome(descriptor(node), result, node.calls())
                log("node=" + node.token + " result=" + result)
                if (result !is RunNodeResult.Done) {
                    primary = result
                    stopPolicy = node.policy.skipPolicy
                    if (result is RunNodeResult.Frozen) node.abort(result.reason)
                } else if (index == nodes.lastIndex) {
                    current = null
                }
            }
        } catch (cancelled: CancellationException) {
            current?.let { node ->
                runCatching { node.abort("cancelled") }
            }
            withContext(NonCancellable) { runCleanup(cleanup, outcomes) }
            throw cancelled
        } finally {
            current = null
            synchronized(gate) { running = false }
        }

        val cleanupOutcome = withContext(NonCancellable) { runCleanup(cleanup, outcomes) }
        if (cleanupOutcome != null && cleanupOutcome.result !is RunNodeResult.Done) {
            primary = RunNodeResult.Frozen("cleanup:" + cleanupOutcome.result)
        }
        val allDone = outcomes.all { it.result is RunNodeResult.Done }
        return PipelineResult(
            outcomes = outcomes.toList(),
            primary = primary,
            cleanup = cleanupOutcome,
            overall = if (allDone) Overall.Ok else Overall.Failed,
        )
    }

    fun abort(reason: String) {
        current?.let { node -> runCatching { node.abort(reason) }.onFailure { log("abort-failed:" + it) } }
    }

    override fun close() {
        val nodes: List<RunNode>
        val cleanup: RunNode?
        synchronized(gate) {
            if (closed) return
            check(!running) { "pipeline busy" }
            closed = true
            nodes = ownedNodes
            cleanup = ownedCleanup
        }
        for (node in nodes.asReversed()) runCatching { node.close() }.onFailure { log("close-failed:" + it) }
        cleanup?.let { runCatching { it.close() }.onFailure { error -> log("cleanup-close-failed:" + error) } }
    }

    private suspend fun execute(node: RunNode): RunNodeResult {
        var attempt = 1
        while (true) {
            val result = try {
                node.run()
            } catch (cancelled: CancellationException) {
                throw cancelled
            } catch (problem: Throwable) {
                return RunNodeResult.Frozen(problem.message ?: problem.javaClass.simpleName)
            }
            if (result is RunNodeResult.Done || result is RunNodeResult.Refused || result is RunNodeResult.Frozen) {
                return result
            }
            if (attempt >= node.policy.attempts) {
                return RunNodeResult.Refused(
                    (result as RunNodeResult.Retryable).reason,
                    mapOf("attempts" to attempt.toString()),
                )
            }
            attempt += 1
            if (node.policy.settleMs > 0L) kotlinx.coroutines.delay(node.policy.settleMs)
        }
    }

    private suspend fun runCleanup(cleanup: RunNode?, outcomes: MutableList<NodeOutcome>): NodeOutcome? {
        if (cleanup == null) return null
        current = cleanup
        val result = try {
            cleanup.run()
        } catch (cancelled: CancellationException) {
            throw cancelled
        } catch (problem: Throwable) {
            RunNodeResult.Frozen(problem.message ?: problem.javaClass.simpleName)
        } finally {
            current = null
        }
        val outcome = NodeOutcome(descriptor(cleanup), result, cleanup.calls())
        outcomes += outcome
        return outcome
    }

    private fun descriptor(node: RunNode): NodeDescriptor =
        NodeDescriptor(node.backendId, node.key, node.token)
}
