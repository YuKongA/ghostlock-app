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
    val recommendShizuku: Boolean,
    /** User-selected Shizuku path for kernels that do not require it. */
    val shizukuEnabled: Boolean = false,
    val shizukuStatus: ShizukuStatus,
)

enum class ShizukuStatus { NOT_REQUIRED, NOT_RUNNING, PERMISSION_REQUIRED, READY }

enum class LogTone { Default, Error, Success, Warning, Progress }

data class LogEntry(val text: String, val tone: LogTone)

data class OffsetCandidate(val release: String, val document: String)

/** One editable advisory execution value (PROFILE-SUGGEST-01 / profile-ui). */
data class ExecutionFieldValue(
    val path: String,
    val value: Long,
    val overridden: Boolean,
)

/** Debug-only export preferences shown by the hidden debug screen. */
data class DebugSettings(
    val exportEnabled: Boolean = true,
    val exportLocation: String = "Download/ghostlock-debug-log",
    val kernelLogEnabled: Boolean = true,
)

/** One node of the resolved profile tree: a JSON group or a numeric leaf. */
data class ProfileFieldNode(
    val path: String,
    val name: String,
    val value: Long? = null,
    /** True when this field or any descendant has an explicit override. */
    val overridden: Boolean = false,
    val children: List<ProfileFieldNode> = emptyList(),
) {
    val isGroup: Boolean get() = children.isNotEmpty()
}

/** Resolved execution view for the advanced editor (controller-owned). */
data class ProfileConfig(
    val release: String,
    val hasProfile: Boolean,
    /** Hierarchical view of every numeric leaf, override flags included. */
    val roots: List<ProfileFieldNode> = emptyList(),
    /** General (execution tuning) subset exposed by the parameters screen. */
    val general: List<ExecutionFieldValue> = emptyList(),
    /** Explicit route from the profile; null means geometry inference. */
    val route: String? = null,
    /** Declared fallback route ("none"/"<route>"); null means unset. */
    val fallbackTo: String? = null,
    /** Dotted paths whose resolved value violates the geometry rules. */
    val invalidPaths: Set<String> = emptySet(),
) {
    companion object {
        /** Routes a profile may declare ("" is the inference fallback). */
        val Routes = listOf("tcp_zerocopy", "select_stack", "multicast_waiter")

        /** Execution tuning paths the general editor exposes. */
        val GeneralPaths = listOf(
            "execution.selected_cpus.main",
            "execution.selected_cpus.consumer",
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
    data class MissingIncludes(val files: List<String>) : OffsetImportResult
    data class Failed(val reason: String) : OffsetImportResult
}

sealed interface ParseResult {
    data class Parsed(val releases: List<String>) : ParseResult
    data class RequiresOverwrite(val releases: List<String>) : ParseResult
    data object AlreadyPresent : ParseResult
    data class Failed(val code: Int, val reason: String? = null) : ParseResult
}
