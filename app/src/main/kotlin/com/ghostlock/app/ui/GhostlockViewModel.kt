package com.ghostlock.app.ui

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.ghostlock.app.R
import com.ghostlock.app.domain.model.KernelSnapshot
import com.ghostlock.app.domain.model.LogTone
import com.ghostlock.app.domain.model.OffsetCandidate
import com.ghostlock.app.domain.model.OffsetImportResult
import com.ghostlock.app.domain.model.ParseResult
import com.ghostlock.app.domain.repository.GhostlockRepository
import com.ghostlock.app.domain.usecase.ExportOffsetsUseCase
import com.ghostlock.app.domain.usecase.FormatLogUseCase
import com.ghostlock.app.domain.usecase.ImportOffsetsUseCase
import com.ghostlock.app.domain.usecase.LoadKernelSnapshotUseCase
import com.ghostlock.app.domain.usecase.ParseSourceUseCase
import com.ghostlock.app.domain.usecase.PublishOffsetsUseCase
import com.ghostlock.app.domain.usecase.ReadDocumentUseCase
import com.ghostlock.app.domain.usecase.RunExploitUseCase
import com.ghostlock.app.domain.usecase.SelectCpuPairUseCase
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.receiveAsFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

sealed interface GhostlockEffect {
    data class PickDocument(val request: DocumentRequest) : GhostlockEffect
    data class Share(val uri: String) : GhostlockEffect
    data class Toast(val resourceId: Int) : GhostlockEffect
    data class Clipboard(val text: String) : GhostlockEffect
    data class KeepScreenAwake(val enabled: Boolean) : GhostlockEffect
}

enum class DocumentRequest { ImportOffsets, BootImage, XblImage }

class GhostlockViewModel(
    private val repository: GhostlockRepository,
) : ViewModel() {
    private val effectChannel = Channel<GhostlockEffect>(Channel.BUFFERED)
    private val mutableState = MutableStateFlow(GhostlockUiState())
    private var initialized = false
    private var running = false
    private val loadKernelSnapshot = LoadKernelSnapshotUseCase(repository)
    private val selectCpuPairUseCase = SelectCpuPairUseCase(repository)
    private val importOffsetsUseCase = ImportOffsetsUseCase(repository)
    private val parseSourceUseCase = ParseSourceUseCase(repository)
    private val exportOffsetsUseCase = ExportOffsetsUseCase(repository)
    private val publishOffsetsUseCase = PublishOffsetsUseCase(repository)
    private val readDocumentUseCase = ReadDocumentUseCase(repository)
    private val runExploitUseCase = RunExploitUseCase(repository)
    private val formatLog = FormatLogUseCase()

    val state = mutableState.asStateFlow()
    val effects = effectChannel.receiveAsFlow()

    private var kernelSnapshot: KernelSnapshot? = null
    private var pendingParseWithXbl = false
    private var pendingBootPath: String? = null
    private var exportCandidates: List<OffsetCandidate> = emptyList()
    private var pendingConfirmation: PendingConfirmation? = null

    fun initialize() {
        if (initialized) return
        initialized = true
        viewModelScope.launch { refreshSnapshot() }
    }

    fun toggleAdvanced() = mutableState.update { it.copy(advancedVisible = !it.advancedVisible) }

    fun selectCpuPair(index: Int) {
        val snapshot = kernelSnapshot ?: return
        if (index !in snapshot.cpuPairs.indices) return
        selectCpuPairUseCase(index)
        kernelSnapshot = snapshot.copy(selectedCpuPair = index)
        mutableState.update { it.copy(cpuPairIndex = index) }
    }

    fun toggleSafeMode(enabled: Boolean) {
        repository.setSafeModeEnabled(enabled)
        mutableState.update { it.copy(safeModeEnabled = enabled) }
    }

    fun toggleTcpRoute(enabled: Boolean) {
        repository.setTcpRouteEnabled(enabled)
        mutableState.update { it.copy(tcpRouteEnabled = enabled) }
    }

    fun onRun() {
        val snapshot = kernelSnapshot ?: return
        if (!snapshot.kernelSupported) {
            if (beginOperation()) {
                appendLog("result: exploit chain unsupported by this kernel")
                endOperation()
            }
            return
        }
        val pair = snapshot.cpuPairs.getOrNull(snapshot.selectedCpuPair) ?: return
        if (!beginOperation()) return
        send(GhostlockEffect.KeepScreenAwake(true))
        appendLog("==== start ====")
        appendLog("cpu pair: ${snapshot.cpuPairLabels.getOrElse(snapshot.selectedCpuPair) { pair.toString() }}")
        viewModelScope.launch(Dispatchers.IO) {
            try {
                val code = runExploitUseCase(pair, ::appendLog)
                appendLog(if (code == 0) "result: exploit completed" else "result: exploit failed (exit code=$code)")
                appendLog("exit code=$code")
            } finally {
                endOperation()
                send(GhostlockEffect.KeepScreenAwake(false))
            }
        }
    }

    fun onCloseExecutionSheet() {
        if (running && !state.value.executionSheetDismissible) return
        mutableState.update { it.copy(executionSheetVisible = false) }
    }

    fun copyLogs() {
        val text = state.value.logLines.joinToString(separator = "") { it.text }
        send(GhostlockEffect.Clipboard(text))
        send(GhostlockEffect.Toast(R.string.copied))
    }

    fun importOffsets() = send(GhostlockEffect.PickDocument(DocumentRequest.ImportOffsets))

    fun parseOffsets() {
        exportCandidates = emptyList()
        mutableState.update {
            it.copy(
                dialogVisible = true,
                dialogType = DialogType.LIST,
                dialogTitleRes = R.string.parse_title,
                dialogItems = emptyList(),
                dialogItemResIds = listOf(R.string.parse_option_boot, R.string.parse_option_boot_xbl),
            )
        }
    }

    fun promptParseUrl() {
        mutableState.update {
            it.copy(
                dialogVisible = true,
                dialogType = DialogType.INPUT,
                dialogTitleRes = R.string.parse_url_title,
                dialogMessageRes = R.string.parse_url_hint,
                dialogInput = "",
            )
        }
    }

    fun exportOffsets() {
        viewModelScope.launch {
            exportCandidates = withContext(Dispatchers.IO) { exportOffsetsUseCase() }
            if (exportCandidates.isEmpty()) {
                send(GhostlockEffect.Toast(R.string.export_none))
            } else {
                mutableState.update {
                    it.copy(
                        dialogVisible = true,
                        dialogType = DialogType.LIST,
                        dialogTitleRes = R.string.export_title,
                        dialogItems = exportCandidates.map(OffsetCandidate::release),
                        dialogItemResIds = emptyList(),
                        dialogCurrentItemIndex = exportCandidates.indexOfFirst { offsetCandidate ->
                            offsetCandidate.release == kernelSnapshot?.kernelRelease
                        },
                    )
                }
            }
        }
    }

    fun onDocumentResult(request: DocumentRequest, uri: String) {
        when (request) {
            DocumentRequest.ImportOffsets -> importDocument(uri)
            DocumentRequest.BootImage -> stageBoot(uri)
            DocumentRequest.XblImage -> stageXbl(uri)
        }
    }

    fun onDialogItemSelected(index: Int) {
        val candidates = exportCandidates
        dismissDialog()
        if (candidates.isNotEmpty()) {
            candidates.getOrNull(index)?.let(::publish)
            exportCandidates = emptyList()
            return
        }
        when (index) {
            0 -> pickBoot(withXbl = false)
            1 -> pickBoot(withXbl = true)
        }
    }

    fun onDialogInputChange(value: String) = mutableState.update { it.copy(dialogInput = value) }

    fun onDialogConfirm(value: String) {
        val dialogType = state.value.dialogType
        dismissDialog(clearConfirmation = false)
        when (dialogType) {
            DialogType.INPUT -> parseUrl(value)
            DialogType.NONE, DialogType.LIST -> Unit
        }
    }

    fun onDialogDismiss() = dismissDialog()

    fun onDialogDismissFinished() {
        if (!state.value.dialogVisible) {
            clearDialog()
        }
    }

    override fun onCleared() {
        repository.close()
        effectChannel.close()
        super.onCleared()
    }

    private suspend fun refreshSnapshot() {
        val snapshot = withContext(Dispatchers.IO) { loadKernelSnapshot() }
        val canExport = withContext(Dispatchers.IO) { exportOffsetsUseCase().isNotEmpty() }
        kernelSnapshot = snapshot
        mutableState.update {
            it.copy(
                deviceName = snapshot.deviceName,
                kernelRelease = snapshot.kernelRelease,
                socName = snapshot.socName,
                kernelSupported = snapshot.kernelSupported,
                cpuPairLabels = snapshot.cpuPairLabels,
                cpuPairIndex = snapshot.selectedCpuPair,
                safeModeEnabled = snapshot.safeModeEnabled,
                tcpRouteEnabled = snapshot.tcpRouteEnabled,
                compact = snapshot.compact,
                exportVisible = canExport,
            )
        }
    }

    private fun importDocument(uri: String) {
        if (!beginOperation()) return
        viewModelScope.launch(Dispatchers.IO) {
            try {
                val json = readDocumentUseCase(uri)
                handleImportResult(importOffsetsUseCase(json), json)
            } catch (error: CancellationException) {
                throw error
            } catch (error: Exception) {
                appendLog("import offsets failed: ${error.message}")
                appendLog("result: import failed")
                send(GhostlockEffect.Toast(R.string.import_failed))
            } finally {
                endOperation()
            }
        }
    }

    private suspend fun handleImportResult(result: OffsetImportResult, json: String) {
        when (result) {
            is OffsetImportResult.RequiresOverwrite -> {
                pendingConfirmation = PendingConfirmation.Import(json)
                showOverwriteDialog(result.releases)
            }

            is OffsetImportResult.Imported -> {
                refreshSnapshot()
                appendLog("offsets.json imported: ${result.releases.joinToString()}")
                appendLog("result: offsets imported successfully")
                send(GhostlockEffect.Toast(R.string.import_success))
            }

            OffsetImportResult.AlreadyPresent -> {
                appendLog("result: offsets already present")
                send(GhostlockEffect.Toast(R.string.offsets_already_exist))
            }
            is OffsetImportResult.Failed -> {
                appendLog("import offsets failed: ${result.reason}")
                appendLog("result: import failed")
                send(GhostlockEffect.Toast(R.string.import_failed))
            }
        }
    }

    private fun pickBoot(withXbl: Boolean) {
        pendingParseWithXbl = withXbl
        if (withXbl) send(GhostlockEffect.Toast(R.string.parse_pick_boot_hint))
        send(GhostlockEffect.PickDocument(DocumentRequest.BootImage))
    }

    private fun stageBoot(uri: String) {
        viewModelScope.launch(Dispatchers.IO) {
            try {
                val bootPath = readDocumentUseCase.cache(uri, "boot.img")
                pendingBootPath = bootPath
                appendLog("boot.img ready: $bootPath")
                if (pendingParseWithXbl) {
                    send(GhostlockEffect.Toast(R.string.parse_pick_xbl_hint))
                    send(GhostlockEffect.PickDocument(DocumentRequest.XblImage))
                } else {
                    runParse(bootPath)
                }
            } catch (error: CancellationException) {
                throw error
            } catch (error: Exception) {
                appendLog("parse error: ${error.message}")
                appendLog("result: parse failed")
                send(GhostlockEffect.Toast(R.string.parse_failed))
            }
        }
    }

    private fun stageXbl(uri: String) {
        viewModelScope.launch(Dispatchers.IO) {
            try {
                val bootPath = requireNotNull(pendingBootPath) { "boot.img is not staged" }
                val xblPath = readDocumentUseCase.cache(uri, "xbl_config.img")
                appendLog("xbl_config.img ready: $xblPath")
                runParse(bootPath, xblPath)
            } catch (error: CancellationException) {
                throw error
            } catch (error: Exception) {
                appendLog("parse error: ${error.message}")
                appendLog("result: parse failed")
                send(GhostlockEffect.Toast(R.string.parse_failed))
            }
        }
    }

    private fun parseUrl(value: String) {
        val url = value.trim()
        if (url.isEmpty() || !(url.startsWith("http://") || url.startsWith("https://"))) {
            appendLog("error: invalid OTA URL: $url")
            appendLog("result: parse failed")
            send(GhostlockEffect.Toast(R.string.parse_failed_url))
            return
        }
        appendLog("parse OTA: $url")
        viewModelScope.launch(Dispatchers.IO) { runParse(url) }
    }

    private suspend fun runParse(input: String, xblPath: String? = null, overwrite: Boolean = false) {
        if (!beginOperation()) return
        try {
            when (val result = parseSourceUseCase(input, xblPath, overwrite, ::appendLog)) {
                is ParseResult.RequiresOverwrite -> {
                    pendingConfirmation = PendingConfirmation.Parse(input, xblPath)
                    showOverwriteDialog(result.releases)
                }

                is ParseResult.Parsed -> {
                    refreshSnapshot()
                    appendLog("offsets.json written: ${result.releases.joinToString()}")
                    appendLog("result: offsets parsed successfully")
                    send(GhostlockEffect.Toast(R.string.parse_success))
                }

                ParseResult.AlreadyPresent -> {
                    appendLog("result: offsets already present")
                    send(GhostlockEffect.Toast(R.string.offsets_already_exist))
                }
                is ParseResult.Failed -> {
                    result.reason?.let { appendLog("parse failed: $it") }
                    appendLog("result: ${parseFailureResult(result.code)}")
                    send(GhostlockEffect.Toast(parseFailureToast(result.code)))
                }
            }
        } finally {
            endOperation()
        }
    }

    fun onOverwriteConfirm() {
        mutableState.update { it.copy(overwriteDialogVisible = false, overwriteMessage = "") }
        confirmPendingOperation()
    }

    fun onOverwriteDismiss() {
        pendingConfirmation = null
        running = false
        appendLog("result: overwrite cancelled")
        mutableState.update {
            it.copy(
                overwriteDialogVisible = false,
                overwriteMessage = "",
                running = false,
                executionSheetDismissible = true,
            )
        }
    }

    private fun confirmPendingOperation() {
        val confirmation = pendingConfirmation ?: return
        pendingConfirmation = null
        when (confirmation) {
            is PendingConfirmation.Import -> {
                if (!beginOperation()) return
                viewModelScope.launch(Dispatchers.IO) {
                    try {
                        handleImportResult(importOffsetsUseCase.overwrite(confirmation.json), confirmation.json)
                    } finally {
                        endOperation()
                    }
                }
            }

            is PendingConfirmation.Parse -> viewModelScope.launch(Dispatchers.IO) {
                runParse(confirmation.input, confirmation.xblPath, overwrite = true)
            }
        }
    }

    private fun publish(candidate: OffsetCandidate) {
        viewModelScope.launch(Dispatchers.IO) {
            try {
                val uri = publishOffsetsUseCase(candidate)
                appendLog("exported offsets: offsets-${candidate.release}.json")
                send(GhostlockEffect.Share(uri))
            } catch (error: CancellationException) {
                throw error
            } catch (error: Exception) {
                appendLog("export offsets failed: ${error.message}")
                send(GhostlockEffect.Toast(R.string.export_failed))
            }
        }
    }

    private fun showOverwriteDialog(releases: List<String>) {
        mutableState.update {
            it.copy(
                overwriteDialogVisible = true,
                overwriteMessage = releases.joinToString("\n"),
            )
        }
    }

    private fun dismissDialog(clearConfirmation: Boolean = true) {
        if (clearConfirmation) pendingConfirmation = null
        mutableState.update {
            it.copy(
                dialogVisible = false,
            )
        }
    }

    private fun clearDialog() {
        mutableState.update {
            it.copy(
                dialogVisible = false,
                dialogType = DialogType.NONE,
                dialogTitleRes = 0,
                dialogMessage = "",
                dialogMessageRes = 0,
                dialogItems = emptyList(),
                dialogItemResIds = emptyList(),
                dialogCurrentItemIndex = -1,
                dialogInput = "",
            )
        }
    }

    private fun appendLog(line: String) {
        val entry = formatLog(line)
        val uiLine = GhostlockLogLine(entry.text, toneColor(entry.tone))
        mutableState.update { it.copy(logLines = it.logLines + uiLine) }
    }

    private fun beginOperation(): Boolean {
        if (running) return false
        running = true
        mutableState.update {
            it.copy(
                running = true,
                executionSheetVisible = true,
                executionSheetDismissible = false,
            )
        }
        return true
    }

    private fun endOperation() {
        running = false
        mutableState.update { it.copy(running = false, executionSheetDismissible = true) }
    }

    private fun send(effect: GhostlockEffect) {
        effectChannel.trySend(effect)
    }

    private fun toneColor(tone: LogTone): Int = when (tone) {
        LogTone.Error -> 0xFFFF6B6B.toInt()
        LogTone.Success -> 0xFF5FD68A.toInt()
        LogTone.Warning -> 0xFFFFC94D.toInt()
        LogTone.Progress -> 0xFF60A5FA.toInt()
        LogTone.Default -> -1
    }

    private fun parseFailureToast(code: Int): Int = when (code) {
        3, 4 -> R.string.parse_failed_route
        5 -> R.string.parse_failed_kallsyms
        6 -> R.string.parse_failed_fixed
        -1 -> R.string.parse_timeout
        else -> R.string.parse_failed
    }

    private fun parseFailureResult(code: Int): String = when (code) {
        3, 4 -> "exploit chain unsupported by this kernel"
        5 -> "kernel symbol table could not be recovered"
        6 -> "kernel has fixed the vulnerability"
        -1 -> "parse timed out"
        else -> "parse failed"
    }

    private sealed interface PendingConfirmation {
        data class Import(val json: String) : PendingConfirmation
        data class Parse(val input: String, val xblPath: String?) : PendingConfirmation
    }
}
