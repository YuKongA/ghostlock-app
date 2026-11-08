package com.ghostlock.app.domain.run

/** Execution lifecycle exposed by the application-scoped run session. */
enum class RunSessionPhase { Idle, Running, Aborting, Aborted, Done, HandoffRequested }

/**
 * Stable read-only prefix of one append-only log row array.
 *
 * Equality uses only O(1) publication identity; neither StateFlow nor Compose walks old rows on append.
 * The owner may append after publication, but never mutates a published prefix.
 */
class LogRowsSnapshot private constructor(
    private val backingRows: ArrayList<LogRecord>,
    private val rowsLock: Any,
    val token: Long,
    val revision: Long,
    val size: Int,
) : Iterable<LogRecord> {
    /** Returns only a row in the prefix fixed at publication. */
    operator fun get(index: Int): LogRecord {
        require(index in 0 until size) { "log row index is outside the published prefix" }
        return synchronized(rowsLock) { backingRows[index] }
    }

    /** @return True when this published prefix has no rows. */
    fun isEmpty(): Boolean = size == 0

    /** Iterates by indexed access without copying the backing history. */
    override fun iterator(): Iterator<LogRecord> = object : Iterator<LogRecord> {
        private var index = 0
        override fun hasNext(): Boolean = index < size
        override fun next(): LogRecord {
            if (!hasNext()) throw NoSuchElementException()
            return get(index++)
        }
    }

    /** Compares scalar publication identity rather than every historical row. */
    override fun equals(other: Any?): Boolean = other is LogRowsSnapshot &&
        token == other.token && revision == other.revision && size == other.size

    /** Hashes scalar publication identity rather than every historical row. */
    override fun hashCode(): Int = 31 * (31 * token.hashCode() + revision.hashCode()) + size

    companion object {
        /** Copies a small external seed such as the idle greeting into a stable snapshot. */
        fun fromRecords(records: List<LogRecord>, token: Long = 0L, revision: Long = 0L): LogRowsSnapshot {
            val rows = ArrayList(records)
            return LogRowsSnapshot(rows, Any(), token, revision, rows.size)
        }

        /** Publishes a fixed-size view of an owner-controlled append-only array without copying it. */
        internal fun fromBacking(
            rows: ArrayList<LogRecord>,
            rowsLock: Any,
            token: Long,
            revision: Long,
        ): LogRowsSnapshot = synchronized(rowsLock) {
            LogRowsSnapshot(rows, rowsLock, token, revision, rows.size)
        }
    }
}

/**
 * Pure projection of the selected backend and its application-scoped execution session.
 *
 * [entries] contains LogHub-committed order only. Optional source clocks are metadata and never reorder this view.
 */
data class RunSessionState(
    val backendKey: String? = null,
    val backendLabel: String = "-",
    val startAllowed: Boolean = false,
    val blockedReasons: List<String> = emptyList(),
    val abortable: Boolean = false,
    val warnOnRerun: Boolean = true,
    val showTimestamp: Boolean = false,
    val minimumLogLevel: LogLevel = LogLevel.Info,
    val terminalFontPath: String? = null,
    val phase: RunSessionPhase = RunSessionPhase.Idle,
    val entries: LogRowsSnapshot = LogRowsSnapshot.fromRecords(initialLogRecords()),
    val report: RunReport? = null,
)

/** Creates the two idle greeting rows under the same minimum-level rule as an accepted run. */
fun initialLogRecords(minimumLevel: LogLevel = LogLevel.Info): List<LogRecord> = listOf(
    LogRecord(LogOrigin.Kotlin, LogLevel.Info, "ghostlock terminal", commitId = 1L),
    LogRecord(LogOrigin.Kotlin, LogLevel.Info, "waiting for execution", commitId = 2L),
).filter { it.level.wireValue >= minimumLevel.wireValue }

/** Typed result returned by a RunHost client and normalized by the outer data adapter. */
sealed interface RunExecutionResult {
    data class Blocked(val reasons: List<String>) : RunExecutionResult
    data class Refused(val reasons: List<String>) : RunExecutionResult
    data class Completed(val report: RunReport) : RunExecutionResult
    data class TransportFailure(val reason: String) : RunExecutionResult
}
