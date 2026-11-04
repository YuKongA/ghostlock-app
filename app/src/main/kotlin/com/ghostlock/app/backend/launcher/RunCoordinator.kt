package com.ghostlock.app.backend.launcher

import android.os.SystemClock
import com.ghostlock.app.domain.run.RunExecutionResult
import com.ghostlock.app.domain.run.RunLogEntry
import com.ghostlock.app.domain.run.RunLogLevel
import com.ghostlock.app.domain.run.RunLogOrigin
import com.ghostlock.app.domain.run.RunSessionPhase
import com.ghostlock.app.domain.run.RunSessionState
import com.ghostlock.app.domain.run.initialRunLogEntries
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

/**
 * Application-scoped owner of one backend launcher transaction and its observable facts.
 *
 * The coordinator deliberately outlives Activity and ViewModel observers. Observer loss therefore
 * detaches collection only; it never cancels the launcher. The only cancellation entry is
 * [requestAbort], which is called for an explicit user action and propagates coroutine cancellation
 * through the existing App/Shizuku launcher contract.
 *
 * @note The coordinator owns the coroutine job; renderers and ViewModels only observe [state].
 * @note Handoff is terminal for observation: late launcher callbacks are ignored after that phase.
 */
class RunCoordinator {
    private val lock = Any()
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
    private val mutableState = MutableStateFlow(RunSessionState())
    private var activeJob: Job? = null
    private var activeGeneration: Long = 0L
    private var abortRequested: Boolean = false

    /** Immutable application-scoped session stream. */
    val state: StateFlow<RunSessionState> = mutableState.asStateFlow()

    /** @return True while the coordinator owns a running or aborting launcher job. */
    fun isRunning(): Boolean = synchronized(lock) {
        activeJob?.isActive == true &&
            (mutableState.value.phase == RunSessionPhase.Running ||
                mutableState.value.phase == RunSessionPhase.Aborting)
    }

    /**
     * Replaces current backend/gate facts without disturbing an active launcher.
     *
     * @param context Fresh adapter projection with phase and entries ignored when selection is stable.
     * @note Selecting a different backend after a terminal run starts a fresh idle terminal surface.
     */
    fun updateContext(context: RunSessionState) {
        synchronized(lock) {
            if (isActivePhase(mutableState.value.phase)) return
            val current = mutableState.value
            val selectionChanged = current.backendKey != context.backendKey
            mutableState.value = context.copy(
                phase = if (selectionChanged) RunSessionPhase.Idle else current.phase,
                entries = if (selectionChanged) initialRunLogEntries() else current.entries,
            )
        }
    }

    /**
     * Starts one application-owned launcher job.
     *
     * @param context Gate-approved immutable facts for the backend being started.
     * @param execute Existing dispatcher invocation; its callback may arrive from a Binder thread.
     * @return True only when no other run is active and the supplied context is startable.
     */
    fun start(
        context: RunSessionState,
        execute: suspend ((String) -> Unit) -> RunExecutionResult,
    ): Boolean {
        lateinit var launched: Job
        val generation: Long
        val startedAt: Long
        synchronized(lock) {
            if (activeJob?.isActive == true || !context.startAllowed || context.backendKey == null) {
                return false
            }
            activeGeneration += 1L
            generation = activeGeneration
            abortRequested = false
            startedAt = SystemClock.elapsedRealtime()
            mutableState.value = context.copy(
                phase = RunSessionPhase.Running,
                entries = listOf(
                    RunLogEntry(
                        origin = RunLogOrigin.Kotlin,
                        level = RunLogLevel.Info,
                        message = "backend run: " + context.backendLabel,
                    ),
                ),
            )
            launched = scope.launch(start = CoroutineStart.LAZY) {
                try {
                    val result = execute { line -> appendRuntimeLine(generation, startedAt, line) }
                    complete(generation, startedAt, result)
                } catch (cancelled: CancellationException) {
                    completeAbort(generation)
                    throw cancelled
                } catch (problem: Throwable) {
                    complete(
                        generation = generation,
                        startedAt = startedAt,
                        result = RunExecutionResult.Refused(
                            listOf(
                                "run-coordinator:exception:" + problem::class.java.simpleName,
                            ),
                        ),
                    )
                } finally {
                    synchronized(lock) {
                        if (activeGeneration == generation && activeJob === launched) {
                            activeJob = null
                            abortRequested = false
                        }
                    }
                }
            }
            activeJob = launched
        }
        launched.start()
        return true
    }

    /**
     * Propagates explicit cancellation to an abortable launcher job.
     *
     * @param disabledMessage Localized warning appended when the backend is non-abortable.
     * @return True when cancellation was accepted; repeated/non-running calls are idempotent false.
     */
    fun requestAbort(disabledMessage: String): Boolean {
        val job: Job
        synchronized(lock) {
            val current = mutableState.value
            job = activeJob ?: return false
            if (!job.isActive || current.phase != RunSessionPhase.Running) return false
            if (!current.abortable) {
                mutableState.value = current.copy(
                    entries = current.entries + RunLogEntry(
                        origin = RunLogOrigin.Kotlin,
                        level = RunLogLevel.Warn,
                        message = disabledMessage,
                    ),
                )
                return false
            }
            abortRequested = true
            mutableState.value = current.copy(phase = RunSessionPhase.Aborting)
        }
        job.cancel(CancellationException("explicit backend abort"))
        return true
    }

    /** Clears retained terminal facts without touching the launcher job or its callbacks. */
    fun clearTerminal() {
        synchronized(lock) {
            mutableState.value = mutableState.value.copy(entries = emptyList())
        }
    }

    /** Appends one live launcher line only while its generation still owns the run. */
    private fun appendRuntimeLine(generation: Long, startedAt: Long, line: String) {
        synchronized(lock) {
            val current = mutableState.value
            if (generation != activeGeneration || !isActivePhase(current.phase)) return
            mutableState.value = current.copy(
                entries = current.entries + RunLogEntry(
                    origin = RunLogOrigin.Kotlin,
                    level = RunLogLevel.Info,
                    message = line,
                    elapsedMs = elapsedSince(startedAt),
                ),
            )
        }
    }

    /** Maps one normalized dispatcher result to terminal facts without re-deciding execution. */
    private fun complete(
        generation: Long,
        startedAt: Long,
        result: RunExecutionResult,
    ) {
        synchronized(lock) {
            if (generation != activeGeneration || abortRequested) return
            val current = mutableState.value
            if (!isActivePhase(current.phase)) return
            val elapsed = elapsedSince(startedAt)
            val additions = when (result) {
                is RunExecutionResult.Blocked -> result.reasons.map { reason ->
                    RunLogEntry(RunLogOrigin.Kotlin, RunLogLevel.Warn, reason, elapsed)
                }

                is RunExecutionResult.Refused -> result.reasons.map { reason ->
                    RunLogEntry(RunLogOrigin.Kotlin, RunLogLevel.Warn, reason, elapsed)
                }

                is RunExecutionResult.Succeeded -> listOf(
                    RunLogEntry(RunLogOrigin.Kotlin, RunLogLevel.Info, "backend run: ok", elapsed),
                ) + evidenceEntries(result.evidence, elapsed)

                is RunExecutionResult.Failed -> listOf(
                    RunLogEntry(
                        RunLogOrigin.Kotlin,
                        RunLogLevel.Warn,
                        "backend run: failed: " + result.reason,
                        elapsed,
                    ),
                ) + evidenceEntries(result.evidence, elapsed)

                RunExecutionResult.HandoffRequested -> listOf(
                    RunLogEntry(
                        RunLogOrigin.Kotlin,
                        RunLogLevel.Info,
                        "backend run: handoff-requested",
                        elapsed,
                    ),
                )

                RunExecutionResult.Idle -> listOf(
                    RunLogEntry(RunLogOrigin.Kotlin, RunLogLevel.Info, "backend run: idle", elapsed),
                )

                RunExecutionResult.Running -> listOf(
                    RunLogEntry(RunLogOrigin.Kotlin, RunLogLevel.Info, "backend run: running", elapsed),
                )
            }
            val phase = when (result) {
                is RunExecutionResult.Blocked -> RunSessionPhase.Idle
                RunExecutionResult.HandoffRequested -> RunSessionPhase.HandoffRequested
                else -> RunSessionPhase.Done
            }
            mutableState.value = current.copy(
                phase = phase,
                entries = current.entries + additions,
            )
        }
    }

    /** Publishes the explicit-abort terminal only for the still-current generation. */
    private fun completeAbort(generation: Long) {
        synchronized(lock) {
            if (generation != activeGeneration || !abortRequested) return
            mutableState.value = mutableState.value.copy(phase = RunSessionPhase.Aborted)
        }
    }

    /** @return Stable evidence rows in the supplied map iteration order. */
    private fun evidenceEntries(evidence: Map<String, String>, elapsed: Long): List<RunLogEntry> =
        evidence.map { (key, value) ->
            RunLogEntry(RunLogOrigin.Kotlin, RunLogLevel.Info, key + " = " + value, elapsed)
        }

    /** @return Non-negative elapsed milliseconds from the monotonic Android clock. */
    private fun elapsedSince(startedAt: Long): Long =
        (SystemClock.elapsedRealtime() - startedAt).coerceAtLeast(0L)

    /** @return Whether callbacks may still mutate the current physical-run observation window. */
    private fun isActivePhase(phase: RunSessionPhase): Boolean =
        phase == RunSessionPhase.Running || phase == RunSessionPhase.Aborting
}
