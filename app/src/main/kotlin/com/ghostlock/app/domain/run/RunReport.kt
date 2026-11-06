package com.ghostlock.app.domain.run

/** Registry-owned capability; unresolved is reserved for pre-prepare and synthetic facts. */
enum class RunCapability(val wireValue: Int) {
    UNRESOLVED(0),
    PRODUCT_ROOT(1),
    DIAGNOSTIC_ONLY(2),
    ;

    companion object {
        /** @return Exact stable value, or null for an unknown wire integer. */
        fun fromWireValue(value: Int): RunCapability? = entries.firstOrNull { candidate -> candidate.wireValue == value }
    }
}

/** Typed outcome of a diagnostic-only backend. */
enum class DiagnosticOutcome(val wireValue: Int) {
    NOT_APPLICABLE(0),
    NOT_RUN(1),
    PASS(2),
    FAIL(3),
    UNKNOWN(4),
    ;

    companion object {
        /** @return Exact stable value, or null for an unknown wire integer. */
        fun fromWireValue(value: Int): DiagnosticOutcome? =
            entries.firstOrNull { candidate -> candidate.wireValue == value }
    }
}

/** Stable state of a backend or aggregate execution stage. */
enum class StageState(val wireValue: Int) {
    NOT_RUN(0),
    RUNNING(1),
    SUCCESS(2),
    FAILED(3),
    CANCELLED(4),
    UNKNOWN(5),
    SKIPPED(6),
    ;

    companion object {
        /** @return Exact stable value, or null for an unknown wire integer. */
        fun fromWireValue(value: Int): StageState? = entries.firstOrNull { candidate -> candidate.wireValue == value }
    }
}

/** Stable state of one Payload or Finish item. */
enum class ItemState(val wireValue: Int) {
    NOT_RUN(0),
    DISPATCHING(1),
    SUCCESS(2),
    FAILED(3),
    SKIPPED_DEPENDENCY(4),
    UNKNOWN(5),
    CANCELLED(6),
    ;

    companion object {
        /** @return Exact stable value, or null for an unknown wire integer. */
        fun fromWireValue(value: Int): ItemState? = entries.firstOrNull { candidate -> candidate.wireValue == value }
    }
}

/** Stable interpretation of [StageResult.exitValue] and item exit values. */
enum class ExitKind(val wireValue: Int) {
    NONE(0),
    PROCESS_EXIT(1),
    SIGNAL(2),
    NATIVE_STATUS(3),
    ;

    companion object {
        /** @return Exact stable value, or null for an unknown wire integer. */
        fun fromWireValue(value: Int): ExitKind? = entries.firstOrNull { candidate -> candidate.wireValue == value }
    }
}

/** Stable reason authority; wire value 12 is deliberately absent and must never be encoded. */
enum class ResultReason(val wireValue: Int) {
    NONE(0),
    NOT_STARTED(1),
    NOT_READY(2),
    REJECTED(3),
    IO_FAILURE(4),
    PROCESS_EXIT_NONZERO(5),
    SIGNALLED(6),
    TIMEOUT(7),
    CANCELLED(8),
    HOST_DIED(9),
    MALFORMED_RESULT(10),
    ARTIFACT_IDENTITY_MISMATCH(11),
    INTERNAL_CONTRACT_FAILURE(13),
    ACK_MISSING(14),
    CLEANUP_INCOMPLETE(15),
    ;

    companion object {
        /** @return Exact non-reserved stable value, or null. */
        fun fromWireValue(value: Int): ResultReason? = entries.firstOrNull { candidate -> candidate.wireValue == value }
    }
}

/** Stable final result of a complete run. */
enum class RunTerminal(val wireValue: Int) {
    NONE(0),
    SUCCESS(1),
    DEGRADED(2),
    FAILED(3),
    CANCELLED(4),
    UNKNOWN(5),
    NEEDS_ATTENTION(6),
    REBOOT_REQUIRED(7),
    REBOOT_REQUESTED(8),
    DIAGNOSTIC_COMPLETE(9),
    ;

    companion object {
        /** @return Exact stable value, or null for an unknown wire integer. */
        fun fromWireValue(value: Int): RunTerminal? = entries.firstOrNull { candidate -> candidate.wireValue == value }
    }
}

/** Backend-only terminal hint; it cannot override Payload or Finish evidence. */
enum class BackendTerminalHint {
    NONE,
    REBOOT_REQUIRED,
    UNKNOWN_HELD_STATE,
}

/**
 * Typed fact for one backend or aggregate stage.
 *
 * @property state Stable stage state.
 * @property reason Stable reason.
 * @property exitKind Explicit exit interpretation.
 * @property exitValue Exit code, signal, or native status according to [exitKind].
 */
data class StageResult(
    val state: StageState,
    val reason: ResultReason,
    val exitKind: ExitKind,
    val exitValue: Int,
) {
    companion object {
        /** Stage has not started. */
        val NOT_RUN: StageResult = StageResult(
            StageState.NOT_RUN,
            ResultReason.NOT_STARTED,
            ExitKind.NONE,
            0,
        )

        /** Stage is intentionally skipped because no work exists. */
        val SKIPPED: StageResult = StageResult(
            StageState.SKIPPED,
            ResultReason.NOT_STARTED,
            ExitKind.NONE,
            0,
        )

        /** Stage completed successfully without a process status. */
        val SUCCESS: StageResult = StageResult(
            StageState.SUCCESS,
            ResultReason.NONE,
            ExitKind.NONE,
            0,
        )
    }
}

/** One ordered Payload item result. */
data class PayloadItemResult(
    val itemId: String,
    val state: ItemState,
    val reason: ResultReason,
    val exitKind: ExitKind,
    val exitValue: Int,
)

/** One ordered Finish item result. */
data class FinishItemResult(
    val itemId: String,
    val state: ItemState,
    val reason: ResultReason,
    val exitKind: ExitKind,
    val exitValue: Int,
)

/**
 * Six-layer terminal authority for one backend run.
 *
 * @property runCapability Immutable registry capability.
 * @property diagnosticOutcome Independent diagnostic outcome.
 * @property backendPrimitive Backend root primitive or diagnostic fixture stage.
 * @property rootScriptDispatch Dispatch of the exact published root script.
 * @property payloadItems Ordered Payload item facts.
 * @property payloadOverall Aggregate Payload fact.
 * @property finishItems Ordered Finish item facts.
 * @property finishOverall Aggregate Finish fact.
 * @property backendCleanup Backend-private safety cleanup fact.
 * @property terminal Stable product or diagnostic terminal.
 */
data class RunReport(
    val runCapability: RunCapability,
    val diagnosticOutcome: DiagnosticOutcome,
    val backendPrimitive: StageResult,
    val rootScriptDispatch: StageResult,
    val payloadItems: List<PayloadItemResult>,
    val payloadOverall: StageResult,
    val finishItems: List<FinishItemResult>,
    val finishOverall: StageResult,
    val backendCleanup: StageResult,
    val terminal: RunTerminal,
) {
    /** Product success is deliberately impossible for diagnostic-only runs. */
    val isProductSuccess: Boolean
        get() = runCapability == RunCapability.PRODUCT_ROOT && terminal == RunTerminal.SUCCESS
}
