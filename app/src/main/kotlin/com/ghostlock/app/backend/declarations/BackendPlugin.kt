package com.ghostlock.app.backend.declarations

import com.ghostlock.app.core.flow.Flow
import com.ghostlock.app.domain.backend.BackendAvailability
import com.ghostlock.app.domain.backend.BackendSpec

/**
 * Internal immutable composition of one neutral spec and its executable providers.
 *
 * @property spec Domain-visible immutable declaration.
 * @property readinessProbe Resource-free dynamic readiness provider.
 * @property flowFactory Creates a fresh one-shot Flow for every accepted run.
 * @property carrierArtifact Optional fixed packaged process carrier declaration.
 * @note This type never crosses UI, persistence, AIDL, or profile boundaries.
 */
internal data class BackendPlugin(
    val spec: BackendSpec,
    val readinessProbe: () -> BackendAvailability,
    val flowFactory: () -> Flow,
    val carrierArtifact: BackendCarrierArtifact? = null,
) {
    /** Evaluates readiness without caching mutable runtime state in [spec]. */
    fun availability(): BackendAvailability = readinessProbe()

    /** Creates one new Flow and never reuses it across runs. */
    fun createFlow(): Flow = flowFactory()
}
