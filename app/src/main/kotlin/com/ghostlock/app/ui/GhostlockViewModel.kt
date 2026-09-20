package com.ghostlock.app.ui

import android.net.Uri
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.ghostlock.app.R
import com.ghostlock.app.domain.model.CpuPair
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
import kotlinx.coroutines.channels.Channel
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
    data class CreateProfileDocument(val suggestedName: String) : GhostlockEffect
    data class Share(val uri: String) : GhostlockEffect
    data class Toast(val resourceId: Int) : GhostlockEffect
    data class Clipboard(val text: String) : GhostlockEffect
    data class KeepScreenAwake(val enabled: Boolean) : GhostlockEffect
    data object OpenShizuku : GhostlockEffect
}

private const val OverwriteSummaryLimit = 12

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
                activeUserProfile = profileController.activeUserProfile(),
                customCpuPair = customCpuPairOf(config),
            )
        }
    }

    /** The pair actually resolved from the profile, when it beats the device pick. */
    private fun customCpuPairOf(config: ProfileConfig): CpuPair? {
        val snapshot = kernelSnapshot ?: return null
        val main = config.general
            .firstOrNull { it.path == "execution.selected_cpus.main" }?.value ?: return null
        val consumer = config.general
            .firstOrNull { it.path == "execution.selected_cpus.consumer" }?.value ?: return null
        val pair = CpuPair(main.toInt(), consumer.toInt())
        return pair.takeIf { it != snapshot.cpuPairs.getOrNull(snapshot.selectedCpuPair) }
    }

    /** Route edits are draft-only; saving the session commits them. */
    fun onRouteChanged(index: Int) {
        val route = ProfileConfig.Routes.getOrNull(index - 1)
        if (route == state.value.profileRoute) return
        mutableState.update { it.copy(profileRoute = route) }
    }

    /** index 0 disables the fallback; the rest map to ProfileConfig.Routes. */
    fun onFallbackChanged(index: Int) {
        val fallback = if (index <= 0) "none" else ProfileConfig.Routes.getOrNull(index - 1)
        val current = state.value.profileFallback
        if (fallback == current || (fallback == "none" && current == null)) return
        mutableState.update { it.copy(profileFallback = fallback) }
    }

    /** General edits are draft-only; saving the session commits them. */
    fun updateExecutionField(path: String, value: String) {
        mutableState.update {
            it.copy(
                executionEditing = it.executionEditing + (path to value),
            )
        }
    }

    /* advanced-ui: the merged screen loads its editors and debug prefs. */
    fun onOpenAdvanced() {
        mutableState.update {
            it.copy(
                advancedScreenVisible = true,
                parametersVisible = false,
                loadConfigVisible = false,
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
                loadConfigVisible = false,
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
                loadConfigVisible = false,
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
                loadConfigVisible = false,
                builtinScreenVisible = false,
                profileOverrideVisible = false,
                advancedOverrideVisible = false,
            )
        }
    }

    /** Opens the configuration-loading screen and refreshes the stored list. */
    fun onOpenLoadConfig() {
        mutableState.update {
            it.copy(
                loadConfigVisible = true,
                builtinScreenVisible = false,
                userProfileDetail = null,
                profileOverrideVisible = false,
                advancedOverrideVisible = false,
            )
        }
        viewModelScope.launch(Dispatchers.IO) { refreshUserProfiles() }
    }

    fun onCloseLoadConfig() {
        mutableState.update {
            it.copy(loadConfigVisible = false, builtinScreenVisible = false, userProfileDetail = null)
        }
    }

    fun onOpenUserProfileDetail(name: String) {
        mutableState.update { it.copy(userProfileDetail = name, builtinScreenVisible = false) }
    }

    fun onCloseUserProfileDetail() {
        mutableState.update { it.copy(userProfileDetail = null) }
    }

    /** Loads a stored document into the imported layer. */
    fun onLoadUserProfile(name: String) = selectUserProfile(name)

    /** Unloads the imported layer, back to built-in plus overrides. */
    fun onUnloadUserProfile() = selectUserProfile(null)

    /**
     * Loads the document and opens the shared parameter-override editor, so
     * "modify" reuses the exact same UI as the parameter overrides.
     */

    private fun selectUserProfile(name: String?) {
        val snapshot = kernelSnapshot ?: return
        val pair = snapshot.cpuPairs.getOrNull(snapshot.selectedCpuPair) ?: return
        viewModelScope.launch(Dispatchers.IO) {
            val config = runCatching {
                profileController.selectUserProfile(name, snapshot.kernelRelease, pair)
            }.getOrNull()
            if (config == null) {
                send(GhostlockEffect.Toast(R.string.user_profile_load_failed))
                return@launch
            }
            applyExecutionConfig(config, preserveEditing = false)
            applyAdvancedConfig(config, preserveEditing = false)
            refreshSnapshot()
            send(
                GhostlockEffect.Toast(
                    if (name != null) R.string.user_profile_loaded
                    else R.string.user_profile_unloaded,
                ),
            )
        }
    }

    private suspend fun refreshUserProfiles() {
        val profiles = runCatching { repository.userProfiles() }.getOrDefault(emptyList())
        mutableState.update { it.copy(userProfiles = profiles) }
    }

    /** Renames a stored document through the shared text-input dialog. */
    fun onUserProfileRename(name: String) {
        mutableState.update {
            it.copy(
                userProfileRenameTarget = name,
                dialogVisible = true,
                dialogType = DialogType.INPUT,
                dialogTitleRes = R.string.user_profile_rename,
                dialogMessageRes = R.string.user_profile_rename_hint,
                dialogInput = name,
                dialogConfirmLabelRes = R.string.user_profile_rename_confirm,
            )
        }
    }

    private fun renameUserProfile(name: String, newName: String) {
        viewModelScope.launch(Dispatchers.IO) {
            val renamed = runCatching { repository.renameUserProfile(name, newName) }.getOrNull()
            refreshUserProfiles()
            if (renamed != null) {
                mutableState.update { state ->
                    state.copy(
                        activeUserProfile = if (state.activeUserProfile == name) {
                            renamed
                        } else {
                            state.activeUserProfile
                        },
                        userProfileDetail = if (state.userProfileDetail == name) {
                            renamed
                        } else {
                            state.userProfileDetail
                        },
                    )
                }
            }
            send(
                GhostlockEffect.Toast(
                    if (renamed != null) R.string.user_profile_renamed
                    else R.string.user_profile_rename_failed,
                ),
            )
        }
    }

    /** Renders the stored document as HOCON and shares it. */
    fun onUserProfileExport(name: String) {
        viewModelScope.launch(Dispatchers.IO) {
            runCatching { repository.exportUserProfile(name) }
                .onSuccess { uri -> send(GhostlockEffect.Share(uri)) }
                .onFailure {
                    android.util.Log.e("GhostLock", "export user profile failed", it)
                    send(GhostlockEffect.Toast(R.string.export_failed))
                }
        }
    }

    /** Converts a legacy document into a current-layout copy in the store. */
    fun onConvertUserProfile(name: String) {
        viewModelScope.launch(Dispatchers.IO) {
            val converted = runCatching { repository.convertUserProfile(name) }.getOrNull()
            refreshUserProfiles()
            send(
                GhostlockEffect.Toast(
                    if (converted != null) R.string.user_profile_converted
                    else R.string.user_profile_convert_failed,
                ),
            )
        }
    }

    fun onUserProfileDelete(name: String) {
        mutableState.update { it.copy(userProfileDeleteTarget = name) }
    }

    fun onUserProfileDeleteConfirm() {
        val name = state.value.userProfileDeleteTarget ?: return
        mutableState.update { it.copy(userProfileDeleteTarget = null) }
        viewModelScope.launch(Dispatchers.IO) {
            val ok = runCatching { repository.deleteUserProfile(name) }.getOrDefault(false)
            refreshUserProfiles()
            if (ok) {
                mutableState.update { current ->
                    current.copy(
                        activeUserProfile = current.activeUserProfile?.takeIf { it != name },
                        userProfileDetail = current.userProfileDetail?.takeIf { it != name },
                    )
                }
                refreshSnapshot()
            }
            send(
                GhostlockEffect.Toast(
                    if (ok) R.string.user_profile_deleted else R.string.user_profile_delete_failed,
                ),
            )
        }
    }

    fun onUserProfileDeleteDismiss() {
        mutableState.update { it.copy(userProfileDeleteTarget = null) }
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

    /** Saves the merged profile through the system document dialog. */
    fun onExportProfile() {
        val release = kernelSnapshot?.kernelRelease ?: return
        send(GhostlockEffect.CreateProfileDocument(exportDocumentName(release)))
    }

    fun onExportProfileDocumentPicked(documentUri: String?) {
        if (documentUri.isNullOrBlank()) return
        val snapshot = kernelSnapshot ?: return
        val pair = snapshot.cpuPairs.getOrNull(snapshot.selectedCpuPair) ?: return
        viewModelScope.launch(Dispatchers.IO) {
            val ok = profileController.export(snapshot.kernelRelease, pair, documentUri)
            send(
                GhostlockEffect.Toast(
                    if (ok) R.string.override_export_done else R.string.export_failed,
                ),
            )
        }
    }

    private fun exportDocumentName(release: String): String =
        "${release.replace(Regex("[^A-Za-z0-9._-]"), "_")}.conf"

    /* ---- editing session: draft edits plus an isolated controller ---- */

    private fun currentPair(): CpuPair? {
        val snapshot = kernelSnapshot ?: return null
        return snapshot.cpuPairs.getOrNull(snapshot.selectedCpuPair)
    }

    /** Opens the editor for the profile the attack controller currently loads. */
    fun onOpenProfileOverrides() {
        openEditSession(profileController.activeUserProfile(), fromDetail = false)
    }

    /**
     * Opens the editor for the stored document [name] without loading it into
     * the attack controller: a private session controller resolves the edits.
     */
    fun onEditUserProfile(name: String) {
        val profile = state.value.userProfiles.firstOrNull { it.name == name }
        if (profile?.version == 1) {
            send(GhostlockEffect.Toast(R.string.user_profile_legacy_hint))
            return
        }
        openEditSession(name, fromDetail = true)
    }

    private fun openEditSession(name: String?, fromDetail: Boolean) {
        viewModelScope.launch(Dispatchers.IO) {
            val config = runCatching { repository.beginEditSession(name) }.getOrNull()
            if (config == null) {
                send(GhostlockEffect.Toast(R.string.user_profile_load_failed))
                return@launch
            }
            applyExecutionConfig(config, preserveEditing = false)
            applyAdvancedConfig(config, preserveEditing = false)
            mutableState.update {
                it.copy(
                    builtinScreenVisible = false,
                    advancedOverrideVisible = false,
                    profileOverrideVisible = true,
                    editTargetName = name,
                    userProfileDetail = if (fromDetail) it.userProfileDetail else null,
                    loadConfigVisible = if (fromDetail) it.loadConfigVisible else false,
                )
            }
        }
    }

    /** Reloads the session, dropping every unsaved edit. */
    fun onRevertProfileEdits() {
        loadEditSession()
        send(GhostlockEffect.Toast(R.string.profile_reverted))
    }

    private fun loadEditSession(preserveEditing: Boolean = false) {
        val snapshot = kernelSnapshot ?: return
        val pair = snapshot.cpuPairs.getOrNull(snapshot.selectedCpuPair) ?: return
        val session = repository.editSessionController() ?: return
        viewModelScope.launch(Dispatchers.IO) {
            val config = runCatching { session.load(snapshot.kernelRelease, pair) }.getOrNull()
                ?: return@launch
            applyExecutionConfig(config, preserveEditing)
            applyAdvancedConfig(config, preserveEditing)
        }
    }

    /** Writes the draft (general, route/fallback, advanced) into the session. */
    private suspend fun commitDraftToSession(): ProfileConfig? {
        val snapshot = kernelSnapshot ?: return null
        val pair = snapshot.cpuPairs.getOrNull(snapshot.selectedCpuPair) ?: return null
        val session = repository.editSessionController() ?: return null
        val release = snapshot.kernelRelease
        val general = mutableState.value.executionEditing.mapNotNull { (path, text) ->
            text.trim().toLongOrNull()?.let { value -> path to value }
        }.toMap()
        session.updateGeneral(release, pair, general)
        session.updateRoute(release, pair, mutableState.value.profileRoute)
        session.updateFallback(release, pair, mutableState.value.profileFallback)
        /* General edits share the execution.* tree paths. The advanced rebuild
         * replaces the whole override entry, so the drafts must be merged in or
         * the general edits would be dropped. */
        val drafts = mutableState.value.profileOverrideEditing +
            mutableState.value.executionEditing
        val advanced = drafts.mapNotNull { (path, text) ->
            text.trim().toLongOrNull()?.let { value -> path to value }
        }.toMap()
        return runCatching { session.updateAdvanced(release, pair, advanced) }.getOrNull()
    }

    /**
     * Commits the draft: a loaded profile gets the overrides written back to
     * the live controller, an unloaded one is stored as a new profile.
     */
    fun onSaveProfileEdits() {
        val pair = currentPair() ?: return
        viewModelScope.launch(Dispatchers.IO) {
            val config = commitDraftToSession()
            if (config == null) {
                send(GhostlockEffect.Toast(R.string.execution_save_failed))
                return@launch
            }
            if (repository.editSessionIsLive()) {
                val committed = runCatching { repository.commitEditSession() }
                    .getOrDefault(false)
                if (!committed) {
                    send(GhostlockEffect.Toast(R.string.execution_save_failed))
                    return@launch
                }
                val live = runCatching { profileController.load(config.release, pair) }.getOrNull()
                if (live != null) {
                    applyExecutionConfig(live, preserveEditing = false)
                    applyAdvancedConfig(live, preserveEditing = false)
                }
                send(GhostlockEffect.Toast(R.string.profile_saved))
            } else if (repository.editSessionTarget() != null) {
                /* Editing a stored document that is not loaded: Save updates
                 * that document in place, Save as creates a copy. */
                val saved = runCatching { repository.saveEditSessionInPlace() }
                    .getOrDefault(false)
                refreshUserProfiles()
                send(
                    GhostlockEffect.Toast(
                        if (saved) R.string.profile_saved else R.string.execution_save_failed,
                    ),
                )
            } else {
                val name = repository.saveEditSessionAsNew()
                refreshUserProfiles()
                send(
                    GhostlockEffect.Toast(
                        if (name != null) R.string.profile_saved_as_new
                        else R.string.execution_save_failed,
                    ),
                )
            }
        }
    }

    /** Stores the session result as a new saved profile. */
    fun onSaveProfileAs() {
        viewModelScope.launch(Dispatchers.IO) {
            if (commitDraftToSession() == null) {
                send(GhostlockEffect.Toast(R.string.execution_save_failed))
                return@launch
            }
            val name = repository.saveEditSessionAsNew()
            refreshUserProfiles()
            send(
                GhostlockEffect.Toast(
                    if (name != null) R.string.profile_saved_as_new
                    else R.string.execution_save_failed,
                ),
            )
        }
    }

    /** Renders the session result as HOCON and shares it. */
    fun onExportProfileEdits() {
        viewModelScope.launch(Dispatchers.IO) {
            if (commitDraftToSession() == null) {
                send(GhostlockEffect.Toast(R.string.export_failed))
                return@launch
            }
            runCatching { repository.exportEditSession() }
                .onSuccess { uri -> send(GhostlockEffect.Share(uri)) }
                .onFailure { send(GhostlockEffect.Toast(R.string.export_failed)) }
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
            /* The controller keeps a single active source: picking a builtin
             * unloads whatever user document was loaded before. */
            val config = runCatching {
                profileController.selectUserProfile(null, snapshot.kernelRelease, pair)
                profileController.selectBuiltin(release, snapshot.kernelRelease, pair)
            }.getOrNull()
            if (config == null) {
                send(GhostlockEffect.Toast(R.string.load_builtin_failed))
                return@launch
            }
            applyExecutionConfig(config, preserveEditing = false)
            applyAdvancedConfig(config, preserveEditing = false)
            refreshSnapshot()
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

    fun onCloseProfileOverrides() {
        repository.endEditSession()
        mutableState.update {
            it.copy(
                profileOverrideVisible = false,
                advancedOverrideVisible = false,
                editTargetName = null,
            )
        }
    }

    fun onOpenAdvancedOverrides() {
        mutableState.update { it.copy(advancedOverrideVisible = true) }
        loadEditSession()
    }

    fun onCloseAdvancedOverrides() {
        mutableState.update { it.copy(advancedOverrideVisible = false) }
    }

    /** Advanced edits are draft-only; saving the session commits them. */
    fun onProfileOverrideChanged(path: String, value: String) {
        mutableState.update {
            it.copy(
                profileOverrideEditing = it.profileOverrideEditing + (path to value),
            )
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
                activeUserProfile = profileController.activeUserProfile(),
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
        /* Picking a preset pair replaces an explicit profile selection. */
        if (mutableState.value.customCpuPair != null) {
            viewModelScope.launch(Dispatchers.IO) {
                val config = runCatching {
                    profileController.clearSelectedCpus(
                        snapshot.kernelRelease,
                        snapshot.cpuPairs[index],
                    )
                }.getOrNull() ?: return@launch
                applyExecutionConfig(config, preserveEditing = false)
                applyAdvancedConfig(config, preserveEditing = false)
            }
        }
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
                dialogConfirmLabelRes = R.string.parse_start,
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
        val renameTarget = state.value.userProfileRenameTarget
        dismissDialog(clearConfirmation = false)
        when {
            renameTarget != null -> renameUserProfile(renameTarget, value)
            dialogType == DialogType.INPUT -> parseUrl(value)
            dialogType == DialogType.CONFIRM -> toggleShizuku(true)
            else -> Unit
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
                customCpuPair = loaded?.let(::customCpuPairOf),
            )
        }
    }

    private fun importDocuments(uris: List<String>) {
        if (uris.isEmpty() || !beginOperation(showLogSheet = false)) return
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
                refreshUserProfiles()
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
                    refreshUserProfiles()
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
                if (!beginOperation(showLogSheet = false)) return
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
                overwriteMessage = overwriteSummary(releases),
                /* The confirmation must be the only overlay on screen; a log
                 * sheet behind it would swallow its taps. */
                executionSheetVisible = false,
            )
        }
    }

    /** Keeps the confirmation readable when a document carries many releases. */
    private fun overwriteSummary(releases: List<String>): String {
        val head = releases.take(OverwriteSummaryLimit).joinToString("\n")
        val rest = releases.size - OverwriteSummaryLimit
        return if (rest > 0) "$head\n… (+$rest)" else head
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
                dialogConfirmLabelRes = R.string.parse_start,
                userProfileRenameTarget = null,
            )
        }
    }

    private fun appendLog(line: String) {
        val entry = formatLog(line)
        val uiLine = GhostlockLogLine(entry.text, toneColor(entry.tone))
        mutableState.update { it.copy(logLines = it.logLines + uiLine) }
    }

    private fun beginOperation(showLogSheet: Boolean = true): Boolean {
        if (running) return false
        running = true
        mutableState.update {
            if (showLogSheet) {
                it.copy(
                    running = true,
                    executionSheetVisible = true,
                    executionSheetDismissible = false,
                )
            } else {
                it.copy(running = true)
            }
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
