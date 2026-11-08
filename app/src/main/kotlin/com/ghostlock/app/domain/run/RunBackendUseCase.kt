package com.ghostlock.app.domain.run

import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.Deferred

/**
 * Single domain entry point for observing and controlling backend execution.
 *
 * @param port Application-scoped run-session boundary implemented by the Android data adapter.
 */
class RunBackendUseCase(
    private val port: RunSessionPort,
) {
    /** Current session stream; observer cancellation only detaches the observer. */
    val state: StateFlow<RunSessionState> = port.state

    /** Refreshes selected-backend and gate facts. */
    fun refresh() = port.refresh()

    /** @return True when a new backend run was accepted. */
    fun start(command: RunCommand): Deferred<Boolean> = port.start(command)

    /** @return True when explicit cancellation was accepted. */
    fun abort(): Boolean = port.abort()

    /** Clears only retained terminal facts. */
    fun clearTerminal() = port.clearTerminal()

    /** @return True when terminal sharing was launched. */
    suspend fun shareTerminal(chooserTitle: String): Boolean = port.shareTerminal(chooserTitle)
}
