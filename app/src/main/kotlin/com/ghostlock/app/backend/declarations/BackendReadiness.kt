package com.ghostlock.app.backend.declarations

/**
 * One declared not-ready fact using an Android resource identifier resolved only by presentation.
 *
 * @property messageRes Stable localized-message resource identifier.
 * @property detail Optional raw evidence retained for diagnostics and excluded from visible text.
 */
data class ReadinessReason(
    val messageRes: Int,
    val detail: String? = null,
)

/** Renderer-neutral readiness declaration owned by one backend plugin. */
sealed interface BackendReadiness {
    /** Backend prerequisites currently pass; notes remain non-blocking facts. */
    data class Ready(val notes: List<ReadinessReason> = emptyList()) : BackendReadiness

    /** Backend prerequisites fail with at least one named reason. */
    data class NotReady(val reasons: List<ReadinessReason>) : BackendReadiness

    /** @return True only for [Ready]. */
    val isReady: Boolean
        get() = this is Ready

    /** @return Notes for [Ready] or blocking reasons for [NotReady]. */
    val blockingReasons: List<ReadinessReason>
        get() = when (this) {
            is Ready -> notes
            is NotReady -> reasons
        }
}

/** Declares whether a backend owns a process-hosted flow entry. */
enum class BackendRunCapability {
    /** Backend can be dispatched through its declared flow symbol. */
    FLOW_RUN,

    /** Backend exposes configuration or handoff capabilities but no run entry. */
    NO_FLOW_RUN,
}
