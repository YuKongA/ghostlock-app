package com.ghostlock.app.core.run

import com.ghostlock.app.core.runhost.RunHostIdentity
import com.ghostlock.app.domain.backend.BackendDescriptor
import com.ghostlock.app.domain.privilege.AttackExecutorKind
import com.ghostlock.app.domain.run.LogConnector
import com.ghostlock.app.domain.run.LogConnectorEnd
import com.ghostlock.app.domain.run.RunExecutionResult

/** Stage at which Android input preparation failed before LogHub acceptance. */
enum class RunDispatchFailureStage {
    FREEZE,
    REQUEST_ASSEMBLY,
}

/** Result of preparing one fixed-target RunHost dispatch lease. */
sealed interface RunDispatchPreparation {
    /** One fully owned request lease ready for LogHub acceptance. */
    data class Prepared(val lease: RunDispatchLease) : RunDispatchPreparation

    /** Stable preparation stage plus the concrete failure type retained for the existing blocked reason. */
    data class Failed(
        val stage: RunDispatchFailureStage,
        val causeTypeName: String,
    ) : RunDispatchPreparation
}

/**
 * Single owner of one assembled RunHost request and all transferred platform resources.
 *
 * @property identity Immutable run identity used by LogHub sink selection.
 * @property backendKey Stable backend key copied from the frozen request.
 * @note [close] is idempotent. Ownership transfers to LogHub only after its accepted commit.
 */
interface RunDispatchLease : AutoCloseable {
    val identity: RunHostIdentity
    val backendKey: String

    /**
     * Runs the already-fixed App or Shizuku target and drains its sole connector.
     *
     * @param attach LogHub connector attachment function for this accepted run.
     * @return Existing transport-normalized execution result.
     */
    suspend fun dispatch(
        attach: suspend (LogConnector) -> LogConnectorEnd,
    ): RunExecutionResult

    /** Closes all request-owned platform resources exactly once. */
    override fun close()
}

/** Pure application boundary for Android freeze, wire assembly, and fixed-target RunHost dispatch. */
interface RunDispatchPort {
    /** Whether the typed Shizuku client required by admission is present in this composition. */
    val shizukuTypedHostAvailable: Boolean

    /**
     * Freezes and assembles one request for the executor captured by admission.
     *
     * @param spec Exact selected backend descriptor.
     * @param target Immutable executor target shared with admission.
     * @param cutoff Actor snapshots captured synchronously by the application coordinator.
     * @return A closeable prepared lease or one explicit pre-acceptance failure stage.
     */
    suspend fun prepare(
        spec: BackendDescriptor,
        target: AttackExecutorKind,
        cutoff: RunInputCutoff,
    ): RunDispatchPreparation
}
