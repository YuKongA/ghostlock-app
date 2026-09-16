package com.ghostlock.app.domain.usecase

import com.ghostlock.app.domain.model.CpuPair
import com.ghostlock.app.domain.repository.GhostlockRepository

class LoadKernelSnapshotUseCase(private val repository: GhostlockRepository) {
    suspend operator fun invoke() = repository.snapshot()
}

class SelectCpuPairUseCase(private val repository: GhostlockRepository) {
    operator fun invoke(index: Int) = repository.selectCpuPair(index)
}

class ImportOffsetsUseCase(private val repository: GhostlockRepository) {
    suspend operator fun invoke(json: String) = repository.importOffsets(json)
    suspend fun overwrite(json: String) = repository.confirmImport(json)
}

class ParseSourceUseCase(private val repository: GhostlockRepository) {
    suspend operator fun invoke(
        input: String,
        xblPath: String? = null,
        overwrite: Boolean = false,
        onLog: (String) -> Unit = {},
    ) = repository.parseSource(input, xblPath, overwrite, onLog)
}

class ExportOffsetsUseCase(private val repository: GhostlockRepository) {
    suspend operator fun invoke() = repository.exportCandidates()
}

class RunExploitUseCase(private val repository: GhostlockRepository) {
    suspend operator fun invoke(pair: CpuPair, onLog: (String) -> Unit) = repository.runExploit(pair, onLog)
}

class ReadDocumentUseCase(private val repository: GhostlockRepository) {
    suspend operator fun invoke(uri: String) = repository.readDocument(uri)
    suspend fun cache(uri: String, fileName: String) = repository.cacheDocument(uri, fileName)
}

class PublishOffsetsUseCase(private val repository: GhostlockRepository) {
    suspend operator fun invoke(candidate: com.ghostlock.app.domain.model.OffsetCandidate) =
        repository.publishOffsets(candidate)
}
