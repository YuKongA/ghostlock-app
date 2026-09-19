package com.ghostlock.app.ui

import android.net.Uri
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.ghostlock.app.R
import com.ghostlock.app.domain.model.KernelSnapshot
import com.ghostlock.app.domain.model.LogTone
import com.ghostlock.app.domain.model.OffsetCandidate
import com.ghostlock.app.domain.model.OffsetImportResult
import com.ghostlock.app.domain.model.ParseResult
import com.ghostlock.app.domain.model.ProfileConfig
import com.ghostlock.app.domain.model.ProfileFieldNode
import com.ghostlock.app.domain.model.ShizukuStatus
import com.ghostlock.app.domain.repository.GhostlockRepository
import com.ghostlock.app.domain.repository.ProfileConfigController
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
import kotlinx.coroutines.Job
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.receiveAsFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import androidx.core.net.toUri
import kotlin.time.Duration.Companion.milliseconds

sealed interface GhostlockEffect {
    data class PickDocument(val request: DocumentRequest) : GhostlockEffect
    data object PickDebugFolder : GhostlockEffect
    data object PickProfileExportFolder : GhostlockEffect
    data class Share(val uri: String) : GhostlockEffect
    data class Toast(val resourceId: Int) : GhostlockEffect
    data class Clipboard(val text: String) : GhostlockEffect
    data class KeepScreenAwake(val enabled: Boolean) : GhostlockEffect
    data object OpenShizuku : GhostlockEffect
}

private const val AutoSaveDelayMillis = 600L

enum class DocumentRequest { ImportOffsetsHocon, ImportOffsetsJson, BootImage, XblImage }

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
    private val profileController get() = repository.profileController()

    val state = mutableState.asStateFlow()
    val effects = effectChannel.receiveAsFlow()

    private var kernelSnapshot: KernelSnapshot? = null
    private var pendingParseWithXbl = false
    private var pendingBootPath: String? = null
    private var exportCandidates: List<OffsetCandidate> = emptyList()
    private var pendingConfirmation: PendingConfirmation? = null
    private var executionSaveJob: Job? = null
    private var profileSaveJob: Job? = null

    fun initialize() {
        if (initialized) return
        initialized = true
        repository.setShizukuStatusListener { refreshAccessStatus() }
        viewModelScope.launch {
            refreshSnapshot()
            applyRecommendedShizuku()
            maybeSuggestShizukuForW3()
        }
    }

    private var recommendedShizukuApplied = false

    /**
     * Kernels that recommend Shizuku start with the toggle on at every launch;
     * a manual switch-off still applies for the rest of the session.
     */
    private fun applyRecommendedShizuku() {
        if (recommendedShizukuApplied) return
        recommendedShizukuApplied = true
        val snapshot = kernelSnapshot ?: return
        if (!snapshot.recommendShizuku || state.value.shizukuEnabled) return
        toggleShizuku(true)
    }

    private var w3HintChecked = false

    /**
     * The previous run's native log survives export settings; when it failed
     * at the W3 seccomp bypass in-process, suggest switching to Shizuku.
     */
    private suspend fun maybeSuggestShizukuForW3() {
        if (w3HintChecked) return
        w3HintChecked = true
        val hint = runCatching { repository.lastRunW3SeccompHint() }.getOrDefault(false)
        if (!hint || state.value.shizukuEnabled) return
        mutableState.update {
            it.copy(
                dialogVisible = true,
                dialogType = DialogType.CONFIRM,
                dialogTitleRes = R.string.w3_shizuku_hint_title,
                dialogMessageRes = R.string.w3_shizuku_hint_message,
            )
        }
    }

    fun refreshAccessStatus() {
        if (initialized) viewModelScope.launch { refreshSnapshot() }
    }

    /* profile-ui: the controller owns loading, merging and persistence. */
    fun loadExecutionProfile(preserveEditing: Boolean = false) {
        val snapshot = kernelSnapshot ?: return
        val pair = snapshot.cpuPairs.getOrNull(snapshot.selectedCpuPair) ?: return
        viewModelScope.launch(Dispatchers.IO) {
            applyExecutionConfig(profileController.load(snapshot.kernelRelease, pair), preserveEditing)
        }
    }

    private fun applyExecutionConfig(config: ProfileConfig, preserveEditing: Boolean) {
        mutableState.update { state ->
            state.copy(
                executionRelease = config.release,
                executionHasProfile = config.hasProfile,
                executionFields = config.general,
                executionEditing = if (preserveEditing) state.executionEditing
                else config.general.associate { field -> field.path to field.value.toString() },
                profileInvalidPaths = config.invalidPaths,
                profileRoute = config.route,
                profileFallback = config.fallbackTo,
                activeBuiltinProfile = profileController.activeBuiltinRelease(),
            )
        }
    }

    /** Switches the explicit route; index 0 restores geometry inference. */
    fun onRouteChanged(index: Int) {
        val snapshot = kernelSnapshot ?: return
        val pair = snapshot.cpuPairs.getOrNull(snapshot.selectedCpuPair) ?: return
        val route = ProfileConfig.Routes.getOrNull(index - 1)
        /* Re-confirming the current value must not rewrite overrides. */
        if (route == state.value.profileRoute) return
        viewModelScope.launch(Dispatchers.IO) {
            val result = runCatching {
                profileController.updateRoute(snapshot.kernelRelease, pair, route)
            }
            val config = result.getOrNull()
            if (config == null) {
                android.util.Log.e("GhostLock", "updateRoute failed", result.exceptionOrNull())
                send(GhostlockEffect.Toast(R.string.execution_save_failed))
                return@launch
            }
            applyExecutionConfig(config, preserveEditing = false)
            applyAdvancedConfig(config, preserveEditing = false)
        }
    }

    /** index 0 disables the fallback; the rest map to ProfileConfig.Routes. */
    fun onFallbackChanged(index: Int) {
        val snapshot = kernelSnapshot ?: return
        val pair = snapshot.cpuPairs.getOrNull(snapshot.selectedCpuPair) ?: return
        val fallback = if (index <= 0) "none" else ProfileConfig.Routes.getOrNull(index - 1)
        val current = state.value.profileFallback
        if (fallback == current || (fallback == "none" && current == null)) return
        viewModelScope.launch(Dispatchers.IO) {
            val result = runCatching {
                profileController.updateFallback(snapshot.kernelRelease, pair, fallback)
            }
            val config = result.getOrNull()
            if (config == null) {
                android.util.Log.e("GhostLock", "updateFallback failed", result.exceptionOrNull())
                send(GhostlockEffect.Toast(R.string.execution_save_failed))
                return@launch
            }
            applyExecutionConfig(config, preserveEditing = false)
            applyAdvancedConfig(config, preserveEditing = false)
        }
    }

    /** General overrides auto-save shortly after the last keystroke. */
    fun updateExecutionField(path: String, value: String) {
        mutableState.update {
            it.copy(
                executionEditing = it.executionEditing + (path to value),
            )
        }
        scheduleExecutionSave()
    }

    private fun scheduleExecutionSave() {
        executionSaveJob?.cancel()
        executionSaveJob = viewModelScope.launch {
            delay(AutoSaveDelayMillis.milliseconds)
            val snapshot = kernelSnapshot ?: return@launch
            val pair = snapshot.cpuPairs.getOrNull(snapshot.selectedCpuPair) ?: return@launch
            val values = mutableState.value.executionEditing.mapNotNull { (path, text) ->
                text.trim().toLongOrNull()?.let { value -> path to value }
            }.toMap()
            val ok = runCatching {
                profileController.updateGeneral(snapshot.kernelRelease, pair, values)
            }.onSuccess { config -> applyExecutionConfig(config, preserveEditing = true) }.isSuccess
            if (!ok) send(GhostlockEffect.Toast(R.string.execution_save_failed))
        }
    }

    /* advanced-ui: the merged screen loads its editors and debug prefs. */
    fun onOpenAdvanced() {
        mutableState.update {
            it.copy(
                advancedScreenVisible = true,
                parametersVisible = false,
                builtinScreenVisible = false,
                profileOverrideVisible = false,
                advancedOverrideVisible = false,
            )
        }
        loadExecutionProfile()
        viewModelScope.launch(Dispatchers.IO) {
            val settings = repository.debugSettings()
            mutableState.update {
                it.copy(
                    debugExportEnabled = settings.exportEnabled,
                    debugExportLocation = settings.exportLocation,
                    debugKernelLogEnabled = settings.kernelLogEnabled,
                )
            }
        }
    }

    fun onCloseAdvanced() {
        mutableState.update {
            it.copy(
                advancedScreenVisible = false,
                parametersVisible = false,
                builtinScreenVisible = false,
                profileOverrideVisible = false,
                advancedOverrideVisible = false,
            )
        }
    }

    fun onOpenParameters() {
        mutableState.update {
            it.copy(
                parametersVisible = true,
                builtinScreenVisible = false,
                profileOverrideVisible = false,
                advancedOverrideVisible = false,
            )
        }
        loadExecutionProfile()
    }

    fun onCloseParameters() {
        mutableState.update {
            it.copy(
                parametersVisible = false,
                builtinScreenVisible = false,
                profileOverrideVisible = false,
                advancedOverrideVisible = false,
            )
        }
    }

    fun onShowAbout() {
        mutableState.update { it.copy(aboutVisible = true) }
    }

    fun onCloseAbout() {
        mutableState.update { it.copy(aboutVisible = false) }
    }

    fun onDebugExportChanged(enabled: Boolean) {
        repository.setDebugExportEnabled(enabled)
        mutableState.update { it.copy(debugExportEnabled = enabled) }
    }

    fun onDebugExportLocationPick() = send(GhostlockEffect.PickDebugFolder)

    fun onDebugExportLocationPicked(location: String?) {
        if (location.isNullOrBlank()) {
            send(GhostlockEffect.Toast(R.string.debug_export_location_unsupported))
            return
        }
        repository.setDebugExportLocation(location)
        mutableState.update { it.copy(debugExportLocation = location) }
    }

    fun onDebugKernelLogChanged(enabled: Boolean) {
        repository.setDebugKernelLogEnabled(enabled)
        mutableState.update { it.copy(debugKernelLogEnabled = enabled) }
    }

    /** Copies the merged profile (HOCON) into a folder the user picks. */
    fun onExportProfile() = send(GhostlockEffect.PickProfileExportFolder)

    fun onExportProfileFolderPicked(folderUri: String?) {
        if (folderUri.isNullOrBlank()) return
        val snapshot = kernelSnapshot ?: return
        val pair = snapshot.cpuPairs.getOrNull(snapshot.selectedCpuPair) ?: return
        viewModelScope.launch(Dispatchers.IO) {
            val ok = profileController.export(snapshot.kernelRelease, pair, folderUri)
            send(
                GhostlockEffect.Toast(
                    if (ok) R.string.override_export_done else R.string.export_failed,
                ),
            )
        }
    }

    /** Drops every general and advanced override back to the resolved defaults. */
    fun onResetParameters() {
        val snapshot = kernelSnapshot ?: return
        val pair = snapshot.cpuPairs.getOrNull(snapshot.selectedCpuPair) ?: return
        viewModelScope.launch(Dispatchers.IO) {
            val config = runCatching {
                profileController.reset(snapshot.kernelRelease, pair)
            }.getOrNull()
            if (config == null) {
                send(GhostlockEffect.Toast(R.string.execution_save_failed))
            } else {
                applyExecutionConfig(config, preserveEditing = false)
                applyAdvancedConfig(config, preserveEditing = false)
                send(GhostlockEffect.Toast(R.string.override_reset_done))
            }
        }
    }

    /** Opens the builtin picker; overrides stay keyed to the device kernel. */
    fun onOpenBuiltinProfiles() {
        val snapshot = kernelSnapshot ?: return
        mutableState.update { it.copy(builtinScreenVisible = true) }
        viewModelScope.launch(Dispatchers.IO) {
            val releases = runCatching { profileController.builtinReleases() }
                .getOrDefault(emptyList())
            val templates = releases.filter {
                it.endsWith(ProfileConfigController.TemplateSuffix)
            }
            val kernels = releases.filterNot {
                it.endsWith(ProfileConfigController.TemplateSuffix)
            }
            if (templates.isEmpty() && kernels.isEmpty()) {
                send(GhostlockEffect.Toast(R.string.load_builtin_failed))
                return@launch
            }
            mutableState.update {
                it.copy(
                    builtinTemplates = sortByKernelSimilarity(
                        snapshot.kernelRelease, templates,
                    ),
                    builtinProfiles = sortByKernelSimilarity(
                        snapshot.kernelRelease, kernels,
                    ),
                )
            }
        }
    }

    fun onCloseBuiltinProfiles() {
        mutableState.update { it.copy(builtinScreenVisible = false) }
    }

    fun onSelectBuiltinProfile(release: String?) {
        val snapshot = kernelSnapshot ?: return
        val pair = snapshot.cpuPairs.getOrNull(snapshot.selectedCpuPair) ?: return
        viewModelScope.launch(Dispatchers.IO) {
            val config = runCatching {
                profileController.selectBuiltin(release, snapshot.kernelRelease, pair)
            }.getOrNull()
            if (config == null) {
                send(GhostlockEffect.Toast(R.string.load_builtin_failed))
                return@launch
            }
            applyExecutionConfig(config, preserveEditing = false)
            applyAdvancedConfig(config, preserveEditing = false)
            send(GhostlockEffect.Toast(R.string.load_builtin_done))
        }
    }

    /** Orders releases by absolute major/minor/fix/android distance to device. */
    private fun sortByKernelSimilarity(deviceRelease: String, releases: List<String>): List<String> {
        val device = kernelVersionKey(deviceRelease)
        return releases.sortedWith(Comparator { a, b ->
            compareIntLists(
                similarityKey(device, kernelVersionKey(a)),
                similarityKey(device, kernelVersionKey(b)),
            )
        })
    }

    private fun kernelVersionKey(release: String): List<Int> {
        val version = release.substringBefore('-').split('.')
            .mapNotNull { it.toIntOrNull() }
        val android = Regex("-android(\\d+)").find(release)
            ?.groupValues?.get(1)?.toIntOrNull()
        return if (android == null) version else version + android
    }

    private fun similarityKey(device: List<Int>, candidate: List<Int>): List<Int> =
        (0 until maxOf(device.size, candidate.size)).map { index ->
            kotlin.math.abs((device.getOrNull(index) ?: 0) - (candidate.getOrNull(index) ?: 0))
        }

    private fun compareIntLists(a: List<Int>, b: List<Int>): Int {
        for (index in 0 until maxOf(a.size, b.size)) {
            val result = (a.getOrNull(index) ?: 0).compareTo(b.getOrNull(index) ?: 0)
            if (result != 0) return result
        }
        return 0
    }

    fun onOpenProfileOverrides() {
        mutableState.update {
            it.copy(profileOverrideVisible = true, advancedOverrideVisible = false)
        }
        loadExecutionProfile()
    }

    fun onCloseProfileOverrides() {
        mutableState.update {
            it.copy(profileOverrideVisible = false, advancedOverrideVisible = false)
        }
    }

    fun onOpenAdvancedOverrides() {
        mutableState.update { it.copy(advancedOverrideVisible = true) }
        loadProfileOverrides()
    }

    fun onCloseAdvancedOverrides() {
        mutableState.update { it.copy(advancedOverrideVisible = false) }
    }

    fun onProfileOverrideChanged(path: String, value: String) {
        mutableState.update {
            it.copy(
                profileOverrideEditing = it.profileOverrideEditing + (path to value),
            )
        }
        scheduleProfileSave()
    }

    private fun scheduleProfileSave() {
        profileSaveJob?.cancel()
        profileSaveJob = viewModelScope.launch {
            delay(AutoSaveDelayMillis.milliseconds)
            val snapshot = kernelSnapshot ?: return@launch
            val pair = snapshot.cpuPairs.getOrNull(snapshot.selectedCpuPair) ?: return@launch
            val values = mutableState.value.profileOverrideEditing.mapNotNull { (path, text) ->
                text.trim().toLongOrNull()?.let { value -> path to value }
            }.toMap()
            val ok = runCatching {
                profileController.updateAdvanced(snapshot.kernelRelease, pair, values)
            }.onSuccess { config -> applyAdvancedConfig(config, preserveEditing = true) }.isSuccess
            if (!ok) send(GhostlockEffect.Toast(R.string.execution_save_failed))
        }
    }

    private fun loadProfileOverrides(preserveEditing: Boolean = false) {
        val snapshot = kernelSnapshot ?: return
        val pair = snapshot.cpuPairs.getOrNull(snapshot.selectedCpuPair) ?: return
        viewModelScope.launch(Dispatchers.IO) {
            applyAdvancedConfig(profileController.load(snapshot.kernelRelease, pair), preserveEditing)
        }
    }

    private fun applyAdvancedConfig(config: ProfileConfig, preserveEditing: Boolean) {
        mutableState.update { state ->
            state.copy(
                profileOverrideRelease = config.release,
                profileOverrideRoots = config.roots,
                profileOverrideEditing = if (preserveEditing) state.profileOverrideEditing
                else flattenLeaves(config.roots).associate { field ->
                    field.path to (field.value?.toString() ?: "")
                },
                profileInvalidPaths = config.invalidPaths,
                profileRoute = config.route,
                profileFallback = config.fallbackTo,
                activeBuiltinProfile = profileController.activeBuiltinRelease(),
            )
        }
    }

    private fun flattenLeaves(nodes: List<ProfileFieldNode>): List<ProfileFieldNode> =
        nodes.flatMap { node -> if (node.isGroup) flattenLeaves(node.children) else listOf(node) }

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

    fun toggleShizuku(enabled: Boolean) {
        repository.setShizukuEnabled(enabled)
        mutableState.update { it.copy(shizukuEnabled = enabled) }
        if (!enabled && kernelSnapshot?.recommendShizuku == true) {
            send(GhostlockEffect.Toast(R.string.shizuku_recommended_hint))
        }
        // The grant dialog lands in another app, so the status is re-read and
        // onResume() refreshes it again when the dialog closes.
        viewModelScope.launch { refreshSnapshot() }
    }

    fun onRun() = runExploit()

    /** Explains why the run button is greyed out. */
    fun onProfileInvalid() {
        val state = state.value
        val messageRes = when {
            !state.executionHasProfile -> R.string.run_blocked_no_profile
            state.shizukuEnabled && state.shizukuStatus != ShizukuStatus.READY ->
                R.string.run_blocked_shizuku
            else -> R.string.profile_invalid
        }
        send(GhostlockEffect.Toast(messageRes))
    }

    fun onStatusClick() {
        val snapshot = kernelSnapshot ?: return
        /* shizukuEnabled already carries the profile suggestion unless the
         * user overrode it (PROFILE-SUGGEST-01). */
        if (!snapshot.shizukuEnabled) return
        when (snapshot.shizukuStatus) {
            ShizukuStatus.NOT_RUNNING -> send(GhostlockEffect.OpenShizuku)
            ShizukuStatus.PERMISSION_REQUIRED -> repository.requestShizukuPermission()
            ShizukuStatus.NOT_REQUIRED,
            ShizukuStatus.READY,
            -> Unit
        }
    }

    private fun runExploit() {
        val snapshot = kernelSnapshot ?: return
        if (!snapshot.kernelSupported) {
            if (beginOperation()) {
                appendLog("result: exploit chain unsupported by this kernel")
                endOperation()
            }
            return
        }
        val useShizuku = snapshot.shizukuEnabled
        if (useShizuku && snapshot.shizukuStatus != ShizukuStatus.READY) {
            onStatusClick()
            return
        }
        val pair = snapshot.cpuPairs.getOrNull(snapshot.selectedCpuPair) ?: return
        if (!beginOperation()) return
        send(GhostlockEffect.KeepScreenAwake(true))
        appendLog("==== start ${if (useShizuku) "Shizuku/V20" else "base"} ====")
        appendLog("cpu pair: ${snapshot.cpuPairLabels.getOrElse(snapshot.selectedCpuPair) { pair.toString() }}")
        viewModelScope.launch(Dispatchers.IO) {
            try {
                val code = runExploitUseCase(pair, useShizuku, ::appendLog)
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

    fun importOffsetsHocon() =
        send(GhostlockEffect.PickDocument(DocumentRequest.ImportOffsetsHocon))

    fun importOffsetsJson() =
        send(GhostlockEffect.PickDocument(DocumentRequest.ImportOffsetsJson))

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
            DocumentRequest.BootImage -> stageBoot(uri)
            DocumentRequest.XblImage -> stageXbl(uri)
            DocumentRequest.ImportOffsetsHocon, DocumentRequest.ImportOffsetsJson -> Unit
        }
    }

    /** Multi-picked documents (a profile plus any include dependencies). */
    fun onDocumentsResult(request: DocumentRequest, uris: List<String>) {
        when (request) {
            DocumentRequest.ImportOffsetsHocon, DocumentRequest.ImportOffsetsJson ->
                importDocuments(uris)

            DocumentRequest.BootImage -> uris.firstOrNull()?.let(::stageBoot)
            DocumentRequest.XblImage -> uris.firstOrNull()?.let(::stageXbl)
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
            DialogType.CONFIRM -> toggleShizuku(true)
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
        /* Validate the resolved profile here so the run button can grey out. */
        val loaded = withContext(Dispatchers.IO) {
            val pair = snapshot.cpuPairs.getOrNull(snapshot.selectedCpuPair)
                ?: return@withContext null
            runCatching { profileController.load(snapshot.kernelRelease, pair) }.getOrNull()
        }
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
                shizukuEnabled = snapshot.shizukuEnabled,
                shizukuStatus = snapshot.shizukuStatus,
                exportVisible = canExport,
                profileInvalidPaths = loaded?.invalidPaths ?: emptySet(),
                executionHasProfile = loaded?.hasProfile ?: false,
            )
        }
    }

    private fun importDocuments(uris: List<String>) {
        if (uris.isEmpty() || !beginOperation()) return
        viewModelScope.launch(Dispatchers.IO) {
            try {
                val documents = linkedMapOf<String, String>()
                uris.forEach { uri ->
                    val name = uri.toUri().lastPathSegment?.let(Uri::decode)
                        ?: uri.substringAfterLast('/')
                    documents[name] = readDocumentUseCase(uri)
                }
                handleImportResult(importOffsetsUseCase(documents), documents)
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

    private suspend fun handleImportResult(
        result: OffsetImportResult,
        documents: Map<String, String>,
    ) {
        when (result) {
            is OffsetImportResult.RequiresOverwrite -> {
                pendingConfirmation = PendingConfirmation.Import(documents)
                showOverwriteDialog(result.releases)
            }

            is OffsetImportResult.Imported -> {
                refreshSnapshot()
                appendLog("profile imported: ${result.releases.joinToString()}")
                appendLog("result: offsets imported successfully")
                val deviceRelease = state.value.kernelRelease
                val matchesDevice = deviceRelease.isEmpty() ||
                    result.releases.any { it == deviceRelease }
                send(
                    GhostlockEffect.Toast(
                        if (matchesDevice) R.string.import_success else R.string.import_no_match,
                    ),
                )
            }

            OffsetImportResult.AlreadyPresent -> {
                appendLog("result: offsets already present")
                send(GhostlockEffect.Toast(R.string.offsets_already_exist))
            }
            is OffsetImportResult.MissingIncludes -> {
                appendLog("import offsets missing includes: ${result.files.joinToString()}")
                appendLog("result: import failed")
                send(GhostlockEffect.Toast(R.string.import_missing_includes))
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
                    appendLog("offsets exported: ${result.releases.joinToString()}")
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
                        handleImportResult(
                            importOffsetsUseCase.overwrite(confirmation.documents),
                            confirmation.documents,
                        )
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
                appendLog("exported offsets: offsets-${candidate.release}.conf")
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
        data class Import(val documents: Map<String, String>) : PendingConfirmation
        data class Parse(val input: String, val xblPath: String?) : PendingConfirmation
    }
}
