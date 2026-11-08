package com.ghostlock.app.domain.finish

import kotlinx.coroutines.Deferred

/** Persistence boundary for the complete independent Finish aggregate. */
interface FinishRepository {
    /** Submits one transform against the actor-current Finish definition. */
    fun submit(transform: (FinishDefinition) -> FinishDefinition): Deferred<FinishDefinition>

    /** @return Exact immutable Finish definition at the current actor cutoff. */
    fun snapshot(): Deferred<FinishDefinition>
}
