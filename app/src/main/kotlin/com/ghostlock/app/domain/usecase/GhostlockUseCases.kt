package com.ghostlock.app.domain.usecase

import com.ghostlock.app.domain.model.CpuPair
import com.ghostlock.app.domain.model.ExecutionMode
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
        uefiPath: String? = null,
        overwrite: Boolean = false,
        onLog: (String) -> Unit = {},
    ) = repository.parseSource(input, xblPath, uefiPath, overwrite, onLog)
}

class RunExploitUseCase(private val repository: GhostlockRepository) {
    /**
     * Dispatches by entry: Shizuku runs through its shell runner; General and
     * UMH run the app-side binary. The selected mode/backend fix the sparse
     * triple in the profile document (43284 -> pagecache_write + umh_forward),
     * and the binary path appends the channel-B frame the 43284 document
     * requires, so UMH never needs the shell.
     */
    suspend operator fun invoke(pair: CpuPair, mode: ExecutionMode, onLog: (String) -> Unit) =
        when (mode) {
            ExecutionMode.General, ExecutionMode.Umh -> repository.runExploit(pair, onLog)
            ExecutionMode.Shizuku -> repository.runExploitWithShizuku(pair, onLog)
        }
}

class ReadDocumentUseCase(private val repository: GhostlockRepository) {
    suspend operator fun invoke(uri: String) = repository.readDocument(uri)
    suspend fun cache(uri: String, fileName: String) = repository.cacheDocument(uri, fileName)
}
