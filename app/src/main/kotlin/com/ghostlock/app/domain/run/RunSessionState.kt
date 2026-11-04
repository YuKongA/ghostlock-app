package com.ghostlock.app.domain.run

/** Execution lifecycle exposed by the application-scoped run session. */
enum class RunSessionPhase {
    /** No backend execution has started for the current selection. */
    Idle,

    /** The launcher owns an active backend execution. */
    Running,

    /** An explicit abort is propagating through the existing launcher cancellation path. */
    Aborting,

    /** The explicit abort completed and the launcher released its current binding. */
    Aborted,

    /** The backend returned a terminal success, failure, or refusal. */
    Done,

    /** The backend requested handoff; the UI must not infer a later terminal result. */
    HandoffRequested,
}

/** Renderer-neutral terminal severity. */
enum class RunLogLevel {
    Verbose,
    Debug,
    Info,
    Warn,
    Error,
}

/** Renderer-neutral terminal origin. */
enum class RunLogOrigin {
    Kotlin,
    Native,
    Shell,
}

/**
 * One immutable terminal fact emitted by the application-scoped session.
 *
 * @property origin Component family that emitted the fact.
 * @property level Severity used by both visual renderers and exported logs.
 * @property message Literal fact body without a presentation prefix.
 * @property elapsedMs Elapsed run time in milliseconds; zero for non-run notices.
 */
data class RunLogEntry(
    val origin: RunLogOrigin,
    val level: RunLogLevel,
    val message: String,
    val elapsedMs: Long = 0L,
)

/**
 * Pure projection of the selected backend and its application-scoped execution session.
 *
 * @property backendKey Stable selected backend identity, or null when none is available.
 * @property backendLabel Human-readable backend identity resolved by the outer adapter.
 * @property startAllowed Result of the existing authoritative run gate for a fresh start.
 * @property blockedReasons Localized gate reasons in stable order.
 * @property abortable Whether explicit user cancellation may propagate to the launcher.
 * @property warnOnRerun Whether presentation must confirm a repeated run.
 * @property showTimestamp Whether terminal rows include elapsed time.
 * @property terminalFontPath Validated custom terminal-font path, or null for monospace.
 * @property phase Current session lifecycle.
 * @property entries Immutable terminal facts retained across renderer recreation.
 * @note This type contains no Android, Compose, backend-plugin, core-flow, or JNI type.
 */
data class RunSessionState(
    val backendKey: String? = null,
    val backendLabel: String = "-",
    val startAllowed: Boolean = false,
    val blockedReasons: List<String> = emptyList(),
    val abortable: Boolean = false,
    val warnOnRerun: Boolean = true,
    val showTimestamp: Boolean = false,
    val terminalFontPath: String? = null,
    val phase: RunSessionPhase = RunSessionPhase.Idle,
    val entries: List<RunLogEntry> = initialRunLogEntries(),
)

/** @return The unchanged two-line terminal greeting for a fresh backend selection. */
fun initialRunLogEntries(): List<RunLogEntry> = listOf(
    RunLogEntry(RunLogOrigin.Kotlin, RunLogLevel.Info, "ghostlock terminal"),
    RunLogEntry(RunLogOrigin.Kotlin, RunLogLevel.Info, "waiting for execution"),
)

/** Result returned by the existing dispatcher and normalized by the outer adapter. */
sealed interface RunExecutionResult {
    /** The authoritative gate rejected dispatch before launcher ownership began. */
    data class Blocked(val reasons: List<String>) : RunExecutionResult

    /** The launcher or host refused the request with stable diagnostic reasons. */
    data class Refused(val reasons: List<String>) : RunExecutionResult

    /** The host returned a successful terminal result and immutable evidence. */
    data class Succeeded(val evidence: Map<String, String>) : RunExecutionResult

    /** The host returned a failed terminal result and immutable evidence. */
    data class Failed(
        val reason: String,
        val evidence: Map<String, String>,
    ) : RunExecutionResult

    /** The host requested handoff; no later success is inferred. */
    data object HandoffRequested : RunExecutionResult

    /** Defensive mapping for an unexpected dispatcher idle state. */
    data object Idle : RunExecutionResult

    /** Defensive mapping for an unexpected dispatcher running state. */
    data object Running : RunExecutionResult
}
