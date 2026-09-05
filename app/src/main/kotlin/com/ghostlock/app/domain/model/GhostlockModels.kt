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
)

enum class LogTone { Default, Error, Success, Warning, Progress }

data class LogEntry(val text: String, val tone: LogTone)

data class OffsetCandidate(val release: String, val json: String)

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
