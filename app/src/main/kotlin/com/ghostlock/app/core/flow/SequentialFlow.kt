package com.ghostlock.app.core.flow

import com.ghostlock.app.log.log
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.TimeoutCancellationException
import kotlinx.coroutines.delay
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeout
import kotlin.random.Random

/**
 * 顺序化 Flow 的模板基类。
 *
 * 子类只提供 steps 与可选 cleanup；run 的顺序、重试、前后延时、超时、最终 cleanup
 * 固定在基类，不允许子类重写。Lua 等不需要顺序模板的实现直接实现 [Flow]。
 */
abstract class SequentialFlow(
    final override val backendClass: kotlin.reflect.KClass<out Flow>,
) : Flow {
    private val lock = Any()
    private var used = false

    /** 由 backend 根据请求组装本次步骤；不得在这里执行步骤。 */
    protected abstract fun steps(request: FlowRequest): List<FlowStep>

    /** 可选清理；在成功、失败、取消路径都只执行一次。 */
    protected open suspend fun cleanup() {}

    final override suspend fun run(request: FlowRequest): FlowResult {
        require(request.backendClass == backendClass) {
            "request backend mismatch: expected=" + backendClass + " actual=" + request.backendClass
        }
        synchronized(lock) {
            check(!used) { "flow is one-shot" }
            used = true
        }
        val planned = try {
            steps(request)
        } catch (cancelled: CancellationException) {
            throw cancelled
        } catch (problem: Throwable) {
            return FlowResult.Failed(
                reason = "flow-plan-exception:" + problem::class.java.simpleName,
                failedStep = null,
                steps = emptyList(),
                evidence = emptyMap(),
            )
        }
        return runSteps(planned)
    }

    override fun stop() {
        // Subclasses with an owned job override this; SequentialFlow itself owns none.
    }

    private suspend fun runSteps(steps: List<FlowStep>): FlowResult {
        val runs = mutableListOf<FlowStepRun>()
        val evidence = linkedMapOf<String, String>()
        var failure: FlowResult.Failed? = null

        try {
            for (spec in steps) {
                val run = runStep(spec, evidence)
                runs += run
                if (run.result is StepResult.Failed) {
                    failure = FlowResult.Failed(
                        reason = run.result.reason,
                        failedStep = spec.name,
                        steps = runs.toList(),
                        evidence = evidence.toMap(),
                    )
                    break
                }
            }
        } catch (cancelled: CancellationException) {
            runCleanup()
            throw cancelled
        }

        val cleanupFailure = runCleanup()
        if (cleanupFailure != null) {
            return FlowResult.Failed(
                reason = cleanupFailure,
                failedStep = "cleanup",
                steps = runs.toList(),
                evidence = evidence.toMap(),
            )
        }
        return failure ?: FlowResult.Ok(runs.toList(), evidence.toMap())
    }

    private suspend fun runStep(spec: FlowStep, evidence: MutableMap<String, String>): FlowStepRun {
        val startedAt = System.nanoTime()
        var attempt = 1
        var last: StepResult = StepResult.Failed("step-not-run")
        while (attempt <= spec.retry.attempts) {
            event("step=" + spec.name + " attempt=" + attempt + " preDelayMs=" + spec.timing.preDelayMs)
            if (spec.timing.preDelayMs > 0L) delay(spec.timing.preDelayMs)

            last = try {
                val body = suspend {
                    spec.step.run(StepContext(spec.name, attempt))
                }
                if (spec.timing.timeoutMs > 0L) {
                    withTimeout(spec.timing.timeoutMs) { body() }
                } else {
                    body()
                }
            } catch (timeout: TimeoutCancellationException) {
                StepResult.Failed("step-timeout:" + spec.name)
            } catch (cancelled: CancellationException) {
                throw cancelled
            } catch (problem: Throwable) {
                StepResult.Failed(
                    "step-exception:" + spec.name + ":" + problem::class.java.simpleName,
                )
            }

            if (spec.timing.postDelayMs > 0L) delay(spec.timing.postDelayMs)

            when (last) {
                is StepResult.Done -> {
                    evidence.putAll(last.evidence)
                    event("step=" + spec.name + " attempt=" + attempt + " done")
                    return FlowStepRun(spec.name, attempt, elapsedMs(startedAt), last)
                }
                is StepResult.Failed -> {
                    event("step=" + spec.name + " attempt=" + attempt + " failed=" + last.reason)
                    return FlowStepRun(spec.name, attempt, elapsedMs(startedAt), last)
                }
                is StepResult.Retry -> {
                    event("step=" + spec.name + " attempt=" + attempt + " retry=" + last.reason)
                    if (attempt >= spec.retry.attempts) {
                        val exhausted = StepResult.Failed(
                            reason = "retry-exhausted:" + spec.name + ":" + last.reason,
                            evidence = mapOf("attempts" to attempt.toString()),
                        )
                        return FlowStepRun(spec.name, attempt, elapsedMs(startedAt), exhausted)
                    }
                    val backoff = (spec.retry.delayMs * attempt)
                        .coerceAtMost(spec.retry.maxDelayMs)
                    val jitter = if (spec.retry.jitterMs > 0L) {
                        Random.nextLong(0L, spec.retry.jitterMs + 1L)
                    } else 0L
                    if (backoff + jitter > 0L) delay(backoff + jitter)
                    attempt++
                }
            }
        }
        return FlowStepRun(spec.name, attempt, elapsedMs(startedAt), last)
    }

    private suspend fun runCleanup(): String? {
        return withContext(NonCancellable) {
            try {
                cleanup()
                null
            } catch (cancelled: CancellationException) {
                throw cancelled
            } catch (problem: Throwable) {
                "cleanup-exception:" + problem::class.java.simpleName
            }
        }
    }

    private fun event(message: String) {
        log.debug(message)
    }

    private fun elapsedMs(startedAt: Long): Long =
        (System.nanoTime() - startedAt) / 1_000_000L
}
