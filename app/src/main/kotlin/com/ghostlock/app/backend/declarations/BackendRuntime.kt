package com.ghostlock.app.backend.declarations

import com.ghostlock.app.core.flow.Flow
import com.ghostlock.app.domain.backend.BackendAvailability
import com.ghostlock.app.domain.backend.BackendDescriptor

/** Runtime behavior owned by one built-in backend without Android UI dependencies. */
internal interface BackendRuntime {
    /** Returns current resource-free readiness without retaining mutable run state. */
    fun availability(): BackendAvailability

    /** Creates a fresh one-shot Flow for an accepted run. */
    fun createFlow(): Flow
}

/**
 * Backend-layer projection derived from the sole outer definition list.
 *
 * @property descriptor Stable domain facts used by gates and typed transport.
 * @property runtime Executable backend implementation.
 * @note This entry is a projection, not a second membership registry.
 */
internal data class BackendRuntimeEntry(
    val descriptor: BackendDescriptor,
    val runtime: BackendRuntime,
) {
    /** Returns current readiness from the owning runtime. */
    fun availability(): BackendAvailability = runtime.availability()

    /** Creates one fresh Flow from the owning runtime. */
    fun createFlow(): Flow = runtime.createFlow()
}
