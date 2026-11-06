package com.ghostlock.app.core.flow

import com.ghostlock.app.domain.run.RunReport

/**
 * Uniform one-shot backend orchestration boundary.
 *
 * @note Backend identity is resolved by the immutable plugin registry; Flow contains no reflection class,
 * method name, Context, Payload/Finish renderer, or product-success inference.
 */
interface Flow {
    /**
     * Runs one backend transaction.
     *
     * @param input Frozen identity, profile, published root script, log emitter, report context, and terminal claim.
     * @return Final typed report composed inside the Flow boundary.
     */
    suspend fun run(input: BackendFlowInput): RunReport

    /** Requests the backend-specific safe stop primitive; no active transaction is a no-op. */
    fun stop()
}
