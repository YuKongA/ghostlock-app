package com.ghostlock.app.domain.run

import kotlinx.coroutines.flow.StateFlow

/** Domain boundary for observing and controlling the application-scoped backend run session. */
interface RunSessionPort {
    /** Current immutable session projection; collection never owns or cancels the physical run. */
    val state: StateFlow<RunSessionState>

    /** Refreshes selected-backend and gate facts without terminating an active run. */
    fun refresh()

    /**
     * Starts one run after re-resolving [command] against the captured registry snapshot.
     *
     * @return True only when the application-scoped coordinator accepted a new run.
     */
    fun start(command: RunCommand): Boolean

    /**
     * Requests explicit cancellation through the current launcher path.
     *
     * @return True when a running abortable session accepted the request.
     */
    fun abort(): Boolean

    /** Clears retained presentation log facts without changing the physical run. */
    fun clearTerminal()

    /**
     * Exports and opens the terminal share sheet through the Android adapter.
     *
     * @param chooserTitle Localized system chooser title supplied by presentation.
     * @return True when the share intent was created and launched.
     */
    fun shareTerminal(chooserTitle: String): Boolean
}
