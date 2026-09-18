package com.ghostlock.app.domain.model

data class CpuPair(val primary: Int, val consumer: Int) {
    override fun toString(): String = "$primary,$consumer"
}

data class KernelSnapshot(
    val deviceName: String,
    val kernelRelease: String,
    val socName: String = "",
    val kernelSupported: Boolean,
    val cpuPairs: List<CpuPair>,
    val cpuPairLabels: List<String>,
    val selectedCpuPair: Int,
    val safeModeEnabled: Boolean,
    /** Profile/imported offsets force the Shizuku path. */
    val requiresShizuku: Boolean,
    /** User-selected Shizuku path for kernels that do not require it. */
    val shizukuEnabled: Boolean = false,
    val shizukuStatus: ShizukuStatus,
)

enum class ShizukuStatus { NOT_REQUIRED, NOT_RUNNING, PERMISSION_REQUIRED, READY }

enum class LogTone { Default, Error, Success, Warning, Progress }

data class LogEntry(val text: String, val tone: LogTone)

data class OffsetCandidate(val release: String, val json: String)

/** One editable advisory execution value (PROFILE-SUGGEST-01 / profile-ui). */
data class ExecutionFieldValue(
    val path: String,
    val value: Long,
    val overridden: Boolean,
)

/** Resolved execution view for the advanced editor. */
data class ExecutionProfile(
    val release: String,
    val hasProfile: Boolean,
    val recommendedMainCpu: Int,
    val recommendedConsumerCpu: Int,
    val fields: List<ExecutionFieldValue>,
) {
    companion object {
        /** Editable subset exposed by the advanced sheet. */
        val EditableFields = listOf(
            "execution.stages.w1_attempts",
            "execution.stages.w2_attempts",
            "execution.stages.w3_attempts",
            "execution.stages.w3_chain_rounds",
            "execution.race.route_wait_ms",
            "execution.heap.prepare_max_attempts",
            "execution.routes.select_stack.enter_delay_us",
            "execution.routes.select_stack.timeout_us",
        )
    }
}

data class KernelOffsets(
    val release: String,
    val scalars: Map<String, Long?>,
    val symbols: Map<String, Long?>,
    val structFields: Map<String, Long?>,
)

sealed interface OffsetImportResult {
    data class Imported(val releases: List<String>) : OffsetImportResult
    data class RequiresOverwrite(val releases: List<String>) : OffsetImportResult
    data object AlreadyPresent : OffsetImportResult
    data class Failed(val reason: String) : OffsetImportResult
}

sealed interface ParseResult {
    data class Parsed(val releases: List<String>) : ParseResult
    data class RequiresOverwrite(val releases: List<String>) : ParseResult
    data object AlreadyPresent : ParseResult
    data class Failed(val code: Int, val reason: String? = null) : ParseResult
}
