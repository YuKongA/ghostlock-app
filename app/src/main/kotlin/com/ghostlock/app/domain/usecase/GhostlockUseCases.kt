package com.ghostlock.app.domain.usecase

import com.ghostlock.app.data.component.BackendKind
import com.ghostlock.app.data.runRequiresShizuku
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
     * Dispatches by **route**, not by mode alone: Shizuku runs through its shell
     * runner and the app-side binary runs the rest. The cve_2026_43284 backend
     * always needs the shell domain (its device-facts collection reads
     * /proc/version and the vendor candidate list, which the app domain cannot),
     * so it is routed through Shizuku even when the mode says General/UMH. The
     * selected mode/backend fix the sparse triple in the profile document
     * (43284 -> pagecache_write + umh_forward), and the shell runner appends the
     * channel-B frame the 43284 document requires.
     */
    suspend operator fun invoke(
        pair: CpuPair,
        mode: ExecutionMode,
        backend: BackendKind,
        onLog: (String) -> Unit,
    ) = if (runRequiresShizuku(mode, backend)) {
        repository.runExploitWithShizuku(pair, onLog)
    } else {
        repository.runExploit(pair, onLog)
    }
}

class ReadDocumentUseCase(private val repository: GhostlockRepository) {
    suspend operator fun invoke(uri: String) = repository.readDocument(uri)
    suspend fun cache(uri: String, fileName: String) = repository.cacheDocument(uri, fileName)
}
