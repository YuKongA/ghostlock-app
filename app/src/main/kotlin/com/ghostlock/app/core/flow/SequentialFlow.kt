package com.ghostlock.app.core.flow

import com.ghostlock.app.domain.run.BackendTerminalHint
import com.ghostlock.app.domain.run.DiagnosticOutcome
import com.ghostlock.app.domain.run.ExitKind
import com.ghostlock.app.domain.run.ResultReason
import com.ghostlock.app.domain.run.LogDraft
import com.ghostlock.app.domain.run.LogLevel
import com.ghostlock.app.domain.run.LogOrigin
import com.ghostlock.app.domain.run.RunReport
import com.ghostlock.app.domain.run.StageResult
import com.ghostlock.app.domain.run.StageState
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.TimeoutCancellationException
import kotlinx.coroutines.delay
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeout
import kotlin.random.Random

/**
 * One-shot sequential Flow template without reflection or backend class identity.
 *
 * Subclasses declare backend-owned primitive/dispatch steps, cleanup, terminal hint, and optional diagnostic
 * outcome. Step notices enter the RunHost-owned Kotlin connector through [BackendFlowInput.logs]; this template
 * never owns source termination. Payload and Finish are absent because they execute inside the published root script.
 */
abstract class SequentialFlow : Flow {
    private val lock = Any()
    private var used: Boolean = false

    /** Plans physically meaningful backend stages for one run. */
    protected abstract fun steps(input: BackendFlowInput): List<FlowStep>

    /** Runs backend-private safety cleanup once on success, failure, or cancellation. */
    protected open suspend fun cleanup(input: BackendFlowInput): StageResult = StageResult.SUCCESS

    /** Returns a backend held-state hint without interpreting product success. */
    protected open fun terminalHint(runs: List<FlowStepRun>): BackendTerminalHint = BackendTerminalHint.NONE

    /** Returns a diagnostic-only outcome from the executed steps and the sole cleanup fact. */
    protected open fun diagnosticOutcome(
        runs: List<FlowStepRun>,
        cleanupFact: StageResult,
    ): DiagnosticOutcome =
        DiagnosticOutcome.NOT_APPLICABLE

    final override suspend fun run(input: BackendFlowInput): RunReport {
        synchronized(lock) {
            check(!used) { "flow is one-shot" }
            used = true
        }
        val facts = try {
            val planned = steps(input)
            validatePlan(planned)
            runSteps(input, planned).facts
        } catch (cancelled: CancellationException) {
            cancelledFacts(input, emptyList())
        } catch (problem: Throwable) {
            notice(input, LogLevel.Error, "flow-plan-failed:" + problem::class.java.simpleName)
            val cleanupFact = runCleanup(input)
            BackendRunFacts(
                primitive = internalUnknown(),
                scriptDispatch = internalUnknown(),
                cleanup = cleanupFact,
                terminalHint = BackendTerminalHint.UNKNOWN_HELD_STATE,
                diagnosticOutcome = DiagnosticOutcome.UNKNOWN,
            )
        }
        return input.complete(facts)
    }

    /** SequentialFlow itself owns no external stop primitive. */
    override fun stop() = Unit

    /** Runs the planned list until the first non-success terminal step fact. */
    private suspend fun runSteps(input: BackendFlowInput, planned: List<FlowStep>): FlowResult {
        val runs = mutableListOf<FlowStepRun>()
        var active: FlowStep? = null
        try {
            for (spec in planned) {
                active = spec
                if (input.cancellation.isCancellationRequested()) {
                    runs += cancelledRun(spec)
                    return FlowResult(cancelledFacts(input, runs), runs.toList())
                }
                val run = runStep(input, spec)
                runs += run
                active = null
                if (!run.facts.allSuccessful()) break
            }
        } catch (cancelled: CancellationException) {
            active?.let { spec -> runs += cancelledRun(spec) }
            return FlowResult(cancelledFacts(input, runs), runs.toList())
        }
        val cleanupFact = runCleanup(input)
        return FlowResult(
            facts = assembleFacts(runs, cleanupFact),
            steps = runs.toList(),
        )
    }

    /** Runs one step under its bounded retry and timing policy. */
    private suspend fun runStep(input: BackendFlowInput, spec: FlowStep): FlowStepRun {
        val startedAt = System.nanoTime()
        var attempt = 1
        var last = FlowStepFacts.forStages(spec.produces, internalUnknown())
        while (attempt <= spec.retry.attempts) {
            notice(input, LogLevel.Debug, "step=${spec.name};attempt=$attempt;phase=start")
            if (spec.timing.preDelayMs > 0L) delay(spec.timing.preDelayMs)
            val result = try {
                val body: suspend () -> StepResult = {
                    spec.step.run(StepContext(input, spec.name, attempt))
                }
                if (spec.timing.timeoutMs > 0L) {
                    withTimeout(spec.timing.timeoutMs) { body() }
                } else {
                    body()
                }
            } catch (timeout: TimeoutCancellationException) {
                StepResult.Failed(
                    FlowStepFacts.forStages(
                        spec.produces,
                        StageResult(StageState.FAILED, ResultReason.TIMEOUT, ExitKind.NONE, 0),
                    ),
                    "step-timeout",
                )
            } catch (cancelled: CancellationException) {
                throw cancelled
            } catch (problem: Throwable) {
                StepResult.Failed(
                    FlowStepFacts.forStages(spec.produces, internalUnknown()),
                    "step-exception:" + problem::class.java.simpleName,
                )
            }
            if (spec.timing.postDelayMs > 0L) delay(spec.timing.postDelayMs)
            when (val exact = exactResult(spec, result)) {
                is StepResult.Done -> {
                    last = exact.facts
                    notice(input, LogLevel.Debug, "step=${spec.name};attempt=$attempt;phase=done")
                    break
                }

                is StepResult.Failed -> {
                    last = exact.facts
                    notice(input, LogLevel.Warn, "step=${spec.name};attempt=$attempt;phase=failed")
                    break
                }

                is StepResult.Retry -> {
                    if (attempt >= spec.retry.attempts) {
                        last = FlowStepFacts.forStages(
                            spec.produces,
                            StageResult(
                                StageState.FAILED,
                                ResultReason.REJECTED,
                                ExitKind.NONE,
                                0,
                            ),
                        )
                        notice(input, LogLevel.Warn, "step=${spec.name};attempt=$attempt;phase=exhausted")
                        break
                    }
                    val backoff = (spec.retry.delayMs * attempt).coerceAtMost(spec.retry.maxDelayMs)
                    val jitter = if (spec.retry.jitterMs > 0L) {
                        Random.nextLong(0L, spec.retry.jitterMs + 1L)
                    } else {
                        0L
                    }
                    if (backoff + jitter > 0L) delay(backoff + jitter)
                    attempt += 1
                }
            }
        }
        return FlowStepRun(
            name = spec.name,
            produces = spec.produces,
            attempts = attempt,
            elapsedMs = (System.nanoTime() - startedAt) / 1_000_000L,
            facts = last,
        )
    }

    /** Rejects duplicate fact writers before the first physical operation starts. */
    private fun validatePlan(planned: List<FlowStep>) {
        val owners = mutableSetOf<FlowStage>()
        planned.forEach { spec ->
            spec.produces.forEach { stage ->
                check(owners.add(stage)) { "duplicate Flow stage producer: $stage" }
            }
        }
    }

    /** Converts missing or undeclared step facts into one exact internal-contract failure. */
    private fun exactResult(spec: FlowStep, result: StepResult): StepResult = when (result) {
        is StepResult.Retry -> result
        is StepResult.Done -> if (result.facts.producedStages() == spec.produces) {
            result
        } else {
            StepResult.Failed(
                FlowStepFacts.forStages(spec.produces, internalUnknown()),
                "step-facts-contract",
            )
        }
        is StepResult.Failed -> if (result.facts.producedStages() == spec.produces) {
            result
        } else {
            StepResult.Failed(
                FlowStepFacts.forStages(spec.produces, internalUnknown()),
                "step-facts-contract",
            )
        }
    }

    /** Returns an unexecuted current step whose complete producer set was cancelled. */
    private fun cancelledRun(spec: FlowStep): FlowStepRun {
        val cancelled = StageResult(StageState.CANCELLED, ResultReason.CANCELLED, ExitKind.NONE, 0)
        return FlowStepRun(
            name = spec.name,
            produces = spec.produces,
            attempts = 0,
            elapsedMs = 0L,
            facts = FlowStepFacts.forStages(spec.produces, cancelled),
        )
    }

    /** Executes cleanup in a non-cancellable section and converts thrown failures to a typed fact. */
    private suspend fun runCleanup(input: BackendFlowInput): StageResult = withContext(NonCancellable) {
        try {
            cleanup(input)
        } catch (problem: Throwable) {
            notice(input, LogLevel.Error, "cleanup-failed:" + problem::class.java.simpleName)
            StageResult(
                StageState.FAILED,
                ResultReason.CLEANUP_INCOMPLETE,
                ExitKind.NONE,
                0,
            )
        }
    }

    /** Builds facts from the last typed result of each owned stage. */
    private fun assembleFacts(runs: List<FlowStepRun>, cleanupFact: StageResult): BackendRunFacts {
        val primitive = runs.asReversed().firstNotNullOfOrNull { run ->
            run.facts.fact(FlowStage.BACKEND_PRIMITIVE)
        } ?: StageResult.SKIPPED
        val dispatch = runs.asReversed().firstNotNullOfOrNull { run ->
            run.facts.fact(FlowStage.ROOT_SCRIPT_DISPATCH)
        } ?: StageResult.SKIPPED
        return BackendRunFacts(
            primitive = primitive,
            scriptDispatch = dispatch,
            cleanup = cleanupFact,
            terminalHint = terminalHint(runs),
            diagnosticOutcome = diagnosticOutcome(runs, cleanupFact),
        )
    }

    /** Returns typed cancellation facts after cleanup completes. */
    private suspend fun cancelledFacts(
        input: BackendFlowInput,
        runs: List<FlowStepRun>,
    ): BackendRunFacts {
        val cleanupFact = runCleanup(input)
        return assembleFacts(runs, cleanupFact)
    }

    /** Publishes one bounded Kotlin log draft without changing control flow. */
    private fun notice(input: BackendFlowInput, level: LogLevel, body: String) {
        runCatching {
            input.logs.emit(
                LogDraft(
                    origin = LogOrigin.Kotlin,
                    level = level,
                    message = body.filter { character -> character.code in 0x20..0x7e }
                        .take(MAX_LOG_CHARACTERS),
                ),
            )
        }
    }

    /** Stable internal-failure fact. */
    private fun internalUnknown(): StageResult = StageResult(
        StageState.UNKNOWN,
        ResultReason.INTERNAL_CONTRACT_FAILURE,
        ExitKind.NONE,
        0,
    )

    private companion object {
        const val MAX_LOG_CHARACTERS: Int = 4 * 1024
    }
}
