package com.ghostlock.app.domain.payload

import kotlinx.coroutines.Deferred

/** Persistence boundary for the complete independent Payload aggregate. */
interface PayloadRepository {
    /** Submits one transform against the DataStore actor's current definition. */
    fun submit(transform: (PayloadDefinition) -> PayloadDefinition): Deferred<PayloadDefinition>

    /** @return Exact immutable definition at the current actor cutoff. */
    fun snapshot(): Deferred<PayloadDefinition>
}
