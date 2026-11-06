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
 * @property minimumLogLevel Frozen minimum severity applied before every log sink.
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
    val minimumLogLevel: RunLogLevel = RunLogLevel.Info,
    val terminalFontPath: String? = null,
    val phase: RunSessionPhase = RunSessionPhase.Idle,
    val entries: List<RunLogRecord> = initialRunLogRecords(),
    val report: RunReport? = null,
)

/**
 * Creates the unchanged two-line terminal greeting under the same minimum-level rule as a run.
 *
 * @param minimumLevel Frozen minimum severity.
 * @return Canonical greeting records visible at [minimumLevel].
 */
fun initialRunLogRecords(minimumLevel: RunLogLevel = RunLogLevel.Info): List<RunLogRecord> = listOf(
    RunLogRecord(RunLogOrigin.Kotlin, RunLogLevel.Info, "ghostlock terminal"),
    RunLogRecord(RunLogOrigin.Kotlin, RunLogLevel.Info, "waiting for execution"),
).filter { record -> record.level.wireValue >= minimumLevel.wireValue }

/** Typed result returned by a RunHost client and normalized by the outer data adapter. */
sealed interface RunExecutionResult {
    /** The authoritative gate rejected dispatch before RunHost ownership began. */
    data class Blocked(val reasons: List<String>) : RunExecutionResult

    /** A synchronous typed RunHost method rejected the request. */
    data class Refused(val reasons: List<String>) : RunExecutionResult

    /** The host returned one immutable six-layer report. */
    data class Completed(val report: RunReport) : RunExecutionResult

    /** Binder death or malformed typed wire produced an explicit unknown terminal. */
    data class TransportFailure(val reason: String) : RunExecutionResult
}
