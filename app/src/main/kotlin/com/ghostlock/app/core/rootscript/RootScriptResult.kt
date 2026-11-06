package com.ghostlock.app.core.rootscript

/** Stable GLR1 item states. */
enum class RootScriptItemState(val wireValue: Int) {
    NotRun(0),
    Dispatching(1),
    Success(2),
    Failed(3),
    SkippedDependency(4),
    Unknown(5),
    Cancelled(6),
    ;

    companion object {
        /** @return Exact wire state, or null for an unsupported integer. */
        fun fromWireValue(value: Int): RootScriptItemState? = entries.firstOrNull { it.wireValue == value }
    }
}

/** Stable GLR1 result reasons; reserved value 12 intentionally has no enum member. */
enum class RootScriptResultReason(val wireValue: Int) {
    None(0),
    NotStarted(1),
    NotReady(2),
    Rejected(3),
    IoFailure(4),
    ProcessExitNonzero(5),
    Signalled(6),
    Timeout(7),
    Cancelled(8),
    HostDied(9),
    MalformedResult(10),
    ArtifactIdentityMismatch(11),
    InternalContractFailure(13),
    AckMissing(14),
    CleanupIncomplete(15),
    ;

    companion object {
        /** @return Exact non-reserved wire reason, or null. */
        fun fromWireValue(value: Int): RootScriptResultReason? = entries.firstOrNull { it.wireValue == value }
    }
}

/** Stable GLR1 item exit kinds. */
enum class RootScriptExitKind(val wireValue: Int) {
    None(0),
    ProcessExit(1),
    Signal(2),
    ;

    companion object {
        /** @return Exact item exit kind, or null. */
        fun fromWireValue(value: Int): RootScriptExitKind? = entries.firstOrNull { it.wireValue == value }
    }
}

/**
 * One strict Payload or Finish item fact.
 *
 * @property itemId Frozen item identity.
 * @property state Stable GLR1 state.
 * @property reason Stable GLR1 reason.
 * @property exitKind Stable process-exit representation.
 * @property exitValue Exit code or signal constrained by [exitKind].
 */
data class RootScriptItemResult(
    val itemId: String,
    val state: RootScriptItemState,
    val reason: RootScriptResultReason,
    val exitKind: RootScriptExitKind,
    val exitValue: Int,
)

/**
 * Complete GLR1 result bound to the frozen request order.
 *
 * @property runId Run identity.
 * @property generation Run generation.
 * @property payloadItems Exact frozen Payload order.
 * @property finishItems Exact frozen Finish order.
 * @property durableHandoff True only after a durable reboot handoff record.
 */
data class RootScriptResult(
    val runId: String,
    val generation: Long,
    val payloadItems: List<RootScriptItemResult>,
    val finishItems: List<RootScriptItemResult>,
    val durableHandoff: Boolean,
)
