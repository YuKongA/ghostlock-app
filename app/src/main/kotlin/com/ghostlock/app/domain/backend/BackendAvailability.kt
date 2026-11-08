package com.ghostlock.app.domain.backend

/**
 * One resource-free readiness fact.
 *
 * @property code Stable diagnostic key.
 * @property message Complete renderer-neutral user text.
 * @property detail Optional diagnostic evidence excluded from ordinary visible text.
 */
data class BackendAvailabilityReason(
    val code: String,
    val message: String,
    val detail: String? = null,
) {
    init {
        require(code.matches(CODE_PATTERN)) { "invalid backend availability reason code: $code" }
        require(message.isNotBlank()) { "backend availability reason message is blank" }
    }

    private companion object {
        val CODE_PATTERN: Regex = Regex("[a-z][a-z0-9._-]{0,63}")
    }
}

/** Dynamic readiness result provided by one backend-owned `BackendRuntime`. */
sealed interface BackendAvailability {
    /** The backend may be prepared; notes are non-blocking diagnostic facts. */
    data class Ready(val notes: List<BackendAvailabilityReason> = emptyList()) : BackendAvailability

    /** The backend may not be prepared; at least one named reason is required. */
    data class NotReady(override val reasons: List<BackendAvailabilityReason>) : BackendAvailability {
        init {
            require(reasons.isNotEmpty()) { "not-ready backend requires a reason" }
        }
    }

    /** True only when the dynamic probe returned [Ready]. */
    val isReady: Boolean
        get() = this is Ready

    /** Notes for a ready backend or blockers for a not-ready backend. */
    val reasons: List<BackendAvailabilityReason>
        get() = when (this) {
            is Ready -> notes
            is NotReady -> reasons
        }
}
