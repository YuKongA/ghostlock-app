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
    suspend operator fun invoke(documents: Map<String, String>) = repository.importOffsets(documents)
    suspend fun overwrite(documents: Map<String, String>) = repository.confirmImport(documents)
}

class ParseSourceUseCase(private val repository: GhostlockRepository) {
    suspend operator fun invoke(
        input: String,
        xblPath: String? = null,
        overwrite: Boolean = false,
        onLog: (String) -> Unit = {},
    ) = repository.parseSource(input, xblPath, overwrite, onLog)
}

class RunExploitUseCase(private val repository: GhostlockRepository) {
    suspend operator fun invoke(pair: CpuPair, useShizuku: Boolean, onLog: (String) -> Unit) =
        if (useShizuku) repository.runExploitWithShizuku(pair, onLog)
        else repository.runExploit(pair, onLog)
}

class ReadDocumentUseCase(private val repository: GhostlockRepository) {
    suspend operator fun invoke(uri: String) = repository.readDocument(uri)
    suspend fun cache(uri: String, fileName: String) = repository.cacheDocument(uri, fileName)
}

class PublishOffsetsUseCase(private val repository: GhostlockRepository) {
    suspend operator fun invoke(candidate: com.ghostlock.app.domain.model.OffsetCandidate) = repository.publishOffsets(candidate)
}
