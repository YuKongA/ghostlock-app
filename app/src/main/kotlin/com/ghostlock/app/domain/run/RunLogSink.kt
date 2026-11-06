package com.ghostlock.app.domain.run

/**
 * Pure-data request for one optional automatic run-log sink.
 *
 * @property enabled Whether the accepted run creates a MediaStore document.
 * @property runId Exact run identity frozen with the host request.
 * @property generation Positive generation paired with [runId].
 * @property backendKey Stable backend key used only as document metadata.
 * @property relativePath Normalized Downloads-relative destination.
 * @property formatConfig Formatting options frozen for the whole document.
 */
data class RunLogSinkRequest(
    val enabled: Boolean,
    val runId: String,
    val generation: Long,
    val backendKey: String,
    val relativePath: String,
    val formatConfig: RunLogFormatConfig,
) {
    init {
        require(runId.isNotBlank()) { "run id must not be blank" }
        require(generation > 0L) { "run generation must be positive" }
        require(backendKey.isNotBlank()) { "backend key must not be blank" }
        require(relativePath.isNotBlank()) { "export path must not be blank" }
    }
}

/** Stable automatic-export failure categories; none can change a [RunReport]. */
enum class RunLogSinkFailureKind {
    OPEN,
    WRITE,
    FLUSH,
    QUEUE_FULL,
    CLOSE,
}

/**
 * Storage-neutral diagnostic for the first failed sink operation.
 *
 * @property kind Stable failing operation.
 * @property detail Bounded implementation-neutral detail suitable for presentation.
 */
data class RunLogSinkFailure(
    val kind: RunLogSinkFailureKind,
    val detail: String,
)

/** Result of the sole non-blocking enqueue operation. */
enum class RunLogSinkEmitResult {
    ACCEPTED,
    REJECTED,
}

/**
 * Run-scoped non-blocking producer handle for one automatic log document.
 *
 * The implementation owns every platform resource. Producers never open, write, flush, close, or wait for the
 * underlying document.
 */
interface RunLogSink {
    /**
     * Copies and enqueues complete UTF-8/LF rows without blocking for platform I/O.
     *
     * @param rows Non-empty rows whose final byte is LF.
     * @return [RunLogSinkEmitResult.ACCEPTED] only when this command entered the sole bounded queue.
     */
    fun tryEmit(rows: List<ByteArray>): RunLogSinkEmitResult

    /** Stops accepting commands and lets the sole worker drain the accepted prefix and close once. */
    fun finish()
}

/** Android-free factory injected into the application coordinator. */
fun interface RunLogSinkFactory {
    /**
     * Creates one run-scoped sink without performing platform I/O on the caller.
     *
     * @param request Frozen accepted-run identity, destination, and formatting configuration.
     * @param onFailure First-failure callback invoked at most once by the returned sink.
     * @return Immediate producer handle whose worker owns all later platform operations.
     * @note Implementations must not call [onFailure] synchronously from this method.
     */
    fun create(
        request: RunLogSinkRequest,
        onFailure: (RunLogSinkFailure) -> Unit,
    ): RunLogSink
}
