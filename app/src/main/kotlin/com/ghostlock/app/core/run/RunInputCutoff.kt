package com.ghostlock.app.core.run

import com.ghostlock.app.domain.finish.FinishDefinition
import com.ghostlock.app.domain.payload.PayloadDefinition
import kotlinx.coroutines.Deferred

/**
 * Exact actor-cutoff handles captured synchronously by one run action.
 *
 * @property backendKey Stable backend key bound to both snapshots.
 * @property payload Immutable Payload transaction result.
 * @property finish Immutable Finish transaction result.
 */
class RunInputCutoff internal constructor(
    val backendKey: String,
    val payload: Deferred<PayloadDefinition>,
    val finish: Deferred<FinishDefinition>,
)
