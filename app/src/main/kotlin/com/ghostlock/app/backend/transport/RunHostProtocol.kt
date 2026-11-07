package com.ghostlock.app.backend.transport

/** Stable synchronous RunHost result codes. */
enum class RunHostCode(val wireValue: Int) {
    OK(0),
    ALREADY_APPLIED(1),
    BAD_STATE(10),
    STALE_GENERATION(11),
    RUN_MISMATCH(12),
    BUSY(13),
    INVALID_REQUEST(14),
    LIMIT_EXCEEDED(15),
    UNKNOWN_BACKEND(16),
    NOT_READY(17),
    ARTIFACT_INVALID(18),
    IO_FAILURE(19),
    UNSAFE_TO_CANCEL(20),
    UNSAFE_TO_DESTROY(21),
    HOST_DIED(22),
    INTERNAL_CONTRACT_FAILURE(23),
    EXECUTION_DOMAIN_MISMATCH(24),
    ;

    companion object {
        /** Unknown values fail closed as [INTERNAL_CONTRACT_FAILURE]. */
        fun fromWireValue(value: Int): RunHostCode =
            entries.firstOrNull { candidate -> candidate.wireValue == value }
                ?: INTERNAL_CONTRACT_FAILURE
    }
}

/** Stable one-shot host lifecycle state. */
enum class RunHostState(val wireValue: Int) {
    NEW(0),
    PREPARED(1),
    RUNNING(2),
    TERMINAL(3),
    DESTROYED(4),
    ;

    companion object {
        /** @return Exact state, or null for an unknown wire integer. */
        fun fromWireValue(value: Int): RunHostState? = entries.firstOrNull { candidate -> candidate.wireValue == value }
    }
}

/** Compound identity required by every callback, snapshot, and control method. */
data class RunHostIdentity(
    val runId: String,
    val generation: Long,
) {
    init {
        require(RunHostProtocol.isRunId(runId)) { "invalid run id" }
        require(generation > 0L) { "generation must be positive" }
    }
}

/** Wire grammar and budget authority shared by validator, service, and both clients. */
object RunHostProtocol {
    const val MAX_REQUEST_PARCEL_BYTES: Int = 256 * 1024
    const val MAX_ARTIFACTS: Int = 32
    const val MAX_ARTIFACT_BYTES: Long = 512L * 1024L * 1024L
    const val MAX_TOTAL_ARTIFACT_BYTES: Long = 1024L * 1024L * 1024L
    const val MAX_PAYLOAD_ITEMS: Int = 64
    const val MAX_FINISH_ITEMS: Int = 32
    const val MAX_ITEM_ARGS: Int = 32
    const val MAX_ARG_BYTES: Int = 4 * 1024
    const val MAX_LOG_MESSAGE_BYTES: Int = 8 * 1024
    const val MAX_LOG_PARCEL_BYTES: Int = 12 * 1024
    const val MAX_SNAPSHOT_PARCEL_BYTES: Int = 256 * 1024
    const val MAX_ROOT_RESULT_BYTES: Int = 256 * 1024

    private val RUN_ID_PATTERN: Regex = Regex("[0-9a-f]{32}")
    private val BACKEND_KEY_PATTERN: Regex = Regex("[A-Za-z0-9][A-Za-z0-9._:-]{0,95}")
    private val LOGICAL_ID_PATTERN: Regex = Regex("[a-z][a-z0-9._-]{0,63}")
    private val SHA_256_PATTERN: Regex = Regex("[0-9a-f]{64}")

    /** True only for an exact 32-character lowercase hexadecimal run identity. */
    fun isRunId(value: String): Boolean = RUN_ID_PATTERN.matches(value)

    /** True only for a bounded immutable-registry key. */
    fun isBackendKey(value: String): Boolean = BACKEND_KEY_PATTERN.matches(value)

    /** True only for a bounded request-global logical identity. */
    fun isLogicalId(value: String): Boolean = LOGICAL_ID_PATTERN.matches(value)

    /** True only for an exact lowercase SHA-256 value. */
    fun isSha256(value: String): Boolean = SHA_256_PATTERN.matches(value)

}

/**
 * Mutex-serialized one-shot lifecycle authority without transport or backend behavior.
 *
 * @note Validation, registry lookup, staging, and callback delivery remain outside this class. Each method's
 * return is the linearization result for the corresponding state transition.
 */
class RunHostStateMachine {
    private val lock = Any()
    private var currentState: RunHostState = RunHostState.NEW
    private var currentIdentity: RunHostIdentity? = null
    private var preparedFingerprint: String? = null
    private var cancellationReserved: Boolean = false

    /** Returns the current lifecycle state under the same mutex used for transitions. */
    fun state(): RunHostState = synchronized(lock) { currentState }

    /** Returns the established identity, or null before callback reservation or prepare. */
    fun identity(): RunHostIdentity? = synchronized(lock) { currentIdentity }

    /**
     * Reserves or replaces the callback identity without retaining a callback object.
     *
     * @param identity Requested compound identity.
     * @return Stable code for the current state and identity.
     */
    fun setCallback(identity: RunHostIdentity): RunHostCode = synchronized(lock) {
        if (currentState == RunHostState.DESTROYED) return@synchronized RunHostCode.BAD_STATE
        identityCode(identity)?.let { code -> return@synchronized code }
        when (currentState) {
            RunHostState.NEW, RunHostState.PREPARED, RunHostState.RUNNING -> {
                if (currentIdentity == null) currentIdentity = identity
                RunHostCode.OK
            }

            RunHostState.TERMINAL -> RunHostCode.ALREADY_APPLIED
            RunHostState.DESTROYED -> RunHostCode.BAD_STATE
        }
    }

    /**
     * Applies state, identity, and semantic-idempotency priority before backend lookup or staging.
     *
     * @param identity Structurally valid request identity.
     * @param fingerprint Normalized typed request fingerprint.
     * @return OK only when prepare may proceed to backend-specific checks and staging.
     */
    fun preparePreflight(identity: RunHostIdentity, fingerprint: String): RunHostCode = synchronized(lock) {
        require(RunHostProtocol.isSha256(fingerprint)) { "invalid normalized request fingerprint" }
        if (currentState == RunHostState.DESTROYED) return@synchronized RunHostCode.BAD_STATE
        identityCode(identity)?.let { code -> return@synchronized code }
        if (preparedFingerprint != null && preparedFingerprint != fingerprint) {
            return@synchronized RunHostCode.RUN_MISMATCH
        }
        when (currentState) {
            RunHostState.NEW -> RunHostCode.OK
            RunHostState.PREPARED -> RunHostCode.ALREADY_APPLIED
            RunHostState.RUNNING, RunHostState.TERMINAL, RunHostState.DESTROYED -> RunHostCode.BAD_STATE
        }
    }

    /**
     * Commits a successful validated prepare and its normalized semantic fingerprint.
     *
     * @param identity Request identity.
     * @param fingerprint Digest of normalized typed fields and artifact metadata, never Parcel bytes or fd numbers.
     * @return Stable transition code.
     */
    fun prepare(identity: RunHostIdentity, fingerprint: String): RunHostCode = synchronized(lock) {
        require(RunHostProtocol.isSha256(fingerprint)) { "invalid normalized request fingerprint" }
        if (currentState == RunHostState.DESTROYED) return@synchronized RunHostCode.BAD_STATE
        identityCode(identity)?.let { code -> return@synchronized code }
        if (preparedFingerprint != null && preparedFingerprint != fingerprint) {
            return@synchronized RunHostCode.RUN_MISMATCH
        }
        when (currentState) {
            RunHostState.NEW -> {
                if (currentIdentity == null) currentIdentity = identity
                preparedFingerprint = fingerprint
                currentState = RunHostState.PREPARED
                RunHostCode.OK
            }

            RunHostState.PREPARED -> RunHostCode.ALREADY_APPLIED
            RunHostState.RUNNING, RunHostState.TERMINAL, RunHostState.DESTROYED -> RunHostCode.BAD_STATE
        }
    }

    /** Starts the already-prepared worker exactly once. */
    fun start(identity: RunHostIdentity): RunHostCode = synchronized(lock) {
        if (currentState == RunHostState.DESTROYED) return@synchronized RunHostCode.BAD_STATE
        identityCode(identity)?.let { code -> return@synchronized code }
        when (currentState) {
            RunHostState.PREPARED -> {
                currentState = RunHostState.RUNNING
                RunHostCode.OK
            }

            RunHostState.RUNNING -> RunHostCode.ALREADY_APPLIED
            RunHostState.NEW, RunHostState.TERMINAL, RunHostState.DESTROYED -> RunHostCode.BAD_STATE
        }
    }

    /** Returns the identity/state eligibility code for a snapshot read. */
    fun snapshotCode(identity: RunHostIdentity): RunHostCode = synchronized(lock) {
        if (currentState == RunHostState.DESTROYED) return@synchronized RunHostCode.BAD_STATE
        identityCode(identity)?.let { code -> return@synchronized code }
        if (currentIdentity == null || currentState == RunHostState.DESTROYED) {
            RunHostCode.BAD_STATE
        } else {
            RunHostCode.OK
        }
    }

    /**
     * Applies a safe cancellation transition or reserves a running cancellation.
     *
     * @param identity Run identity.
     * @param runningCancellationSafe Whether current backend physical work may be stopped.
     * @return Stable code; accepted running work remains RUNNING until worker terminal publication.
     */
    fun cancel(identity: RunHostIdentity, runningCancellationSafe: Boolean): RunHostCode = synchronized(lock) {
        if (currentState == RunHostState.DESTROYED) return@synchronized RunHostCode.BAD_STATE
        identityCode(identity)?.let { code -> return@synchronized code }
        if (currentIdentity == null) return@synchronized RunHostCode.BAD_STATE
        when (currentState) {
            RunHostState.NEW, RunHostState.PREPARED -> {
                cancellationReserved = true
                currentState = RunHostState.TERMINAL
                RunHostCode.OK
            }

            RunHostState.RUNNING -> if (runningCancellationSafe) {
                cancellationReserved = true
                RunHostCode.OK
            } else {
                RunHostCode.UNSAFE_TO_CANCEL
            }

            RunHostState.TERMINAL -> RunHostCode.ALREADY_APPLIED
            RunHostState.DESTROYED -> RunHostCode.BAD_STATE
        }
    }

    /**
     * Claims the sole worker terminal transition and its cancellation decision under one lock.
     *
     * @param identity Exact active run identity.
     * @return Whether cancellation won before the claim, or null when this worker no longer owns RUNNING.
     */
    fun claimTerminal(identity: RunHostIdentity): Boolean? = synchronized(lock) {
        if (identityCode(identity) != null || currentState != RunHostState.RUNNING) return@synchronized null
        currentState = RunHostState.TERMINAL
        cancellationReserved
    }

    /** Applies the explicit close gate used before unbind or framework destroy. */
    fun requestClose(identity: RunHostIdentity): RunHostCode = synchronized(lock) {
        if (currentState == RunHostState.DESTROYED) return@synchronized RunHostCode.ALREADY_APPLIED
        identityCode(identity)?.let { code -> return@synchronized code }
        if (currentIdentity == null) return@synchronized RunHostCode.BAD_STATE
        when (currentState) {
            RunHostState.NEW, RunHostState.PREPARED, RunHostState.TERMINAL -> {
                currentState = RunHostState.DESTROYED
                RunHostCode.OK
            }

            RunHostState.RUNNING -> RunHostCode.UNSAFE_TO_DESTROY
            RunHostState.DESTROYED -> RunHostCode.ALREADY_APPLIED
        }
    }

    /** True only after [requestClose] moved the host to DESTROYED. */
    fun frameworkDestroyAuthorized(): Boolean = synchronized(lock) {
        currentState == RunHostState.DESTROYED
    }

    /**
     * Compares a requested identity to the established one.
     *
     * @return Null for a match or an unestablished identity; otherwise the deterministic mismatch code.
     */
    private fun identityCode(requested: RunHostIdentity): RunHostCode? {
        val current = currentIdentity ?: return null
        return when {
            requested.generation < current.generation -> RunHostCode.STALE_GENERATION
            requested.generation > current.generation -> RunHostCode.BUSY
            requested.runId != current.runId -> RunHostCode.RUN_MISMATCH
            else -> null
        }
    }
}
