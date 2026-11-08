package com.ghostlock.app.domain.settings

import kotlinx.coroutines.Deferred
import kotlinx.coroutines.flow.StateFlow

/** Domain boundary for the two durable application selections. */
interface AppSelectionPort {
    /** Last committed selection projection, seeded before the first UI composition. */
    val selection: StateFlow<AppSelection>

    /**
     * Submits one field-preserving transaction to the sole DataStore actor.
     *
     * @param transform Pure transform over the actor's current value.
     * @return Completion handle whose value is the exact committed aggregate.
     */
    fun submit(transform: (AppSelection) -> AppSelection): Deferred<AppSelection>

    /** Seeds [selection] from the real store exactly once during application initialization. */
    suspend fun awaitInitial(): AppSelection

    /**
     * Queues an identity transaction at the caller's linearization point.
     *
     * @return Completion handle containing the immutable aggregate at that cutoff.
     */
    fun snapshot(): Deferred<AppSelection>
}
