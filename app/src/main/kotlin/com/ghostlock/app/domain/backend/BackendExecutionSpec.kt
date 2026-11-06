package com.ghostlock.app.domain.backend

/** Safe-cancellation declaration owned by one immutable backend specification. */
enum class AbortPolicy {
    /** Cancellation may be delivered while the backend flow is running. */
    SAFE_DURING_RUN,

    /** Cancellation is accepted only before the physical flow starts. */
    BEFORE_START_ONLY,

    /** The physical transaction is never killed by a user cancellation request. */
    NEVER,
}

/** Process identity/domain in which a backend flow is allowed to execute. */
enum class ExecutionDomain {
    /** App UID in the application or dedicated app RunHost process. */
    APP,

    /** Authorized Shizuku shell UID and shell SELinux domain. */
    SHIZUKU_SHELL,

    /** Either supported host, with the actual host still checked mechanically. */
    APP_OR_SHIZUKU,
}

/** Selection exposure policy; readiness remains a separate dynamic fact. */
enum class BackendSelectionPolicy {
    /** The backend may be selected when its readiness probe is ready. */
    SELECTABLE,

    /** The backend is visible for configuration or diagnostics but cannot be selected. */
    DISABLED,

    /** The backend is omitted from ordinary product presentation. */
    HIDDEN,
}
