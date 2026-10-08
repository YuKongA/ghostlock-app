package com.ghostlock.app.ui

import android.net.Uri
import androidx.core.net.toUri
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.ghostlock.app.R
import com.ghostlock.app.data.Cve2026_43284Fields
import com.ghostlock.app.data.component.BackendKind
import com.ghostlock.app.data.component.CombinationCatalog
import com.ghostlock.app.data.payload.PayloadImportResult
import com.ghostlock.app.data.payload.PayloadKind
import com.ghostlock.app.data.plugin.PluginConfigValidator
import com.ghostlock.app.data.plugin.PluginDescriptor
import com.ghostlock.app.data.plugin.PluginManifestEntry
import com.ghostlock.app.data.plugin.PluginImportResult
import com.ghostlock.app.data.plugin.PluginParamType
import com.ghostlock.app.data.plugin.PluginRunSelection
import com.ghostlock.app.data.plugin.PluginValue
import com.ghostlock.app.data.component.CombinationSpec
import com.ghostlock.app.data.isAvailable
import com.ghostlock.app.data.backend
import com.ghostlock.app.data.resolveExecutionSelection
import com.ghostlock.app.data.steps
import com.ghostlock.app.data.toExecutionMode
import com.ghostlock.app.data.requiresShizuku
import com.ghostlock.app.data.runRequiresShizuku
import com.ghostlock.app.domain.model.CpuPair
import com.ghostlock.app.domain.model.ExecutionMode
import com.ghostlock.app.domain.model.KernelSnapshot
import com.ghostlock.app.domain.model.LogTone
import com.ghostlock.app.domain.model.OffsetImportResult
import com.ghostlock.app.domain.model.ParseResult
import com.ghostlock.app.domain.model.ProfileConfig
import com.ghostlock.app.domain.model.ProfileFieldNode
import com.ghostlock.app.domain.model.ShizukuStatus
import com.ghostlock.app.domain.repository.GhostlockRepository
import com.ghostlock.app.domain.repository.ProfileConfigController
import com.ghostlock.app.domain.usecase.FormatLogUseCase
import com.ghostlock.app.domain.usecase.ImportOffsetsUseCase
import com.ghostlock.app.domain.usecase.LoadKernelSnapshotUseCase
import com.ghostlock.app.domain.usecase.ParseSourceUseCase
import com.ghostlock.app.domain.usecase.ReadDocumentUseCase
import com.ghostlock.app.domain.usecase.RunExploitUseCase
import com.ghostlock.app.domain.usecase.SelectCpuPairUseCase
import java.util.Locale
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
    data object PickDebugFolder : GhostlockEffect
    data class CreateProfileDocument(val suggestedName: String) : GhostlockEffect
    data class Share(val uri: String) : GhostlockEffect
    data class Toast(val resourceId: Int) : GhostlockEffect

    /** A localized message with one string argument (the UI resolves it). */
    data class ToastArgs(val resourceId: Int, val arg: String) : GhostlockEffect
    data class Clipboard(val text: String) : GhostlockEffect
    data class KeepScreenAwake(val enabled: Boolean) : GhostlockEffect
    data object OpenShizuku : GhostlockEffect

    /**
     * One FINISHED run: the UI decides and performs the root-manager step
     * ([rootManagerAction]). Sent once per run from the single completion point,
     * so a recomposition can never launch the manager twice.
     *
     * [succeeded] is the App's success criterion and [rootProduced] says whether
     * this run actually left root state behind (`force_attack_test` does not):
     * they stay separate facts, and the pure function decides.
     */
    data class ShowRootManager(
        val succeeded: Boolean,
        val rootProduced: Boolean,
        val packageName: String?,
    ) : GhostlockEffect
}

private const val OverwriteSummaryLimit = 12

enum class DocumentRequest {
    ImportOffsetsHocon,
    ImportOffsetsJson,
    BootImage,
    XblImage,
    PayloadImage,
    UefiImage,
    PluginModule,

    /** payload batch (a): one script file / one or more .ko modules. */
    PayloadScript,
    PayloadKo,
}

private enum class ParseDialogStage { Mode, Attach }


/**
 * The shared text dialog for one plugin parameter (P1).
 *
 * The dialog carries NO message resource: its label is the parameter name as
 * plain text. That is not cosmetic — the first version left the message id at 0
 * and the page rendered it, which crashed the device with
 * `Resources$NotFoundException: String resource ID #0x0`. Keeping the state
 * construction here makes the path unit-testable without an Android runtime.
 */
internal fun pluginParamEditDialogState(
    base: GhostlockUiState,
    id: String,
    name: String,
    current: String,
): GhostlockUiState = base.copy(
    pluginParamEditId = id,
    pluginParamEditName = name,
    dialogVisible = true,
    dialogType = DialogType.INPUT,
    dialogTitleRes = R.string.plugin_param_edit,
    dialogMessage = name,
    dialogMessageRes = null,
    dialogInput = current,
    dialogConfirmLabelRes = R.string.plugin_param_edit,
)

class GhostlockViewModel(
    private val repository: GhostlockRepository,
) : ViewModel() {
    private val effectChannel = Channel<GhostlockEffect>(Channel.BUFFERED)
    private val mutableState = MutableStateFlow(GhostlockUiState())
    private var initialized = false
    /* Release whose execution profile has already been auto-loaded. */
    private var loadedForRelease: String? = null
    private var running = false
    private val loadKernelSnapshot = LoadKernelSnapshotUseCase(repository)
    private val selectCpuPairUseCase = SelectCpuPairUseCase(repository)
    private val importOffsetsUseCase = ImportOffsetsUseCase(repository)
    private val parseSourceUseCase = ParseSourceUseCase(repository)
    private val readDocumentUseCase = ReadDocumentUseCase(repository)
    private val runExploitUseCase = RunExploitUseCase(repository)
    private val formatLog = FormatLogUseCase()
    private val profileController get() = repository.profileController()

    val state = mutableState.asStateFlow()
    val effects = effectChannel.receiveAsFlow()

    private var kernelSnapshot: KernelSnapshot? = null
    private var pendingParseWithXbl = false
    private var pendingParseWithUefi = false
    private var pendingBootPath: String? = null
    private var pendingXblPath: String? = null
    private var pendingUefiPath: String? = null
    private var parseDialogStage = ParseDialogStage.Mode
    private var pendingConfirmation: PendingConfirmation? = null

    fun initialize() {
        if (initialized) return
        initialized = true
        repository.setShizukuStatusListener { refreshAccessStatus() }
        viewModelScope.launch {
            refreshSnapshot()
            maybeSuggestShizukuForW3()
        }
    }

    private var w3HintChecked = false

    /**
     * The previous run's native log survives export settings; when it failed
     * at the W3 seccomp bypass in-process, suggest switching to Shizuku.
     */
    private suspend fun maybeSuggestShizukuForW3() {
        if (w3HintChecked) return
        w3HintChecked = true
        val step = runCatching { repository.lastRunStuckStep() }.getOrNull() ?: return
        if (state.value.executionMode != ExecutionMode.General) return
        /* Any interrupted step is reported; only a W3 stall offers Shizuku,
         * because Shizuku (shell uid, no seccomp) skips exactly that stage. */
        val isW3 = step.startsWith("w3")
        mutableState.update {
            it.copy(
                dialogVisible = true,
                dialogType = if (isW3) DialogType.CONFIRM else DialogType.NOTICE,
                dialogTitleRes = if (isW3) {
                    R.string.w3_shizuku_hint_title
                } else {
                    R.string.run_interrupted_title
                },
                dialogMessageRes = if (isW3) {
                    R.string.w3_shizuku_hint_message
                } else {
                    R.string.run_interrupted_message
                },
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
                declaredCombinations = config.declaredCombinations,

                /* P0: the probe's own reasons a selected plugin blocks the
                 * document — the run gate shows them instead of a generic line. */
                executionPluginErrors = config.pluginErrors,
                executionDeclarationErrors = config.declarationErrors,
                /* (a) The DECLARED default decides the initial selection; the
                 * catalogue default only covers a profile that declares nothing.
                 * An explicit pick is never overridden: only a value still equal
                 * to the catalogue default is replaced (HISTORY: the catalogue
                 * default used to win over the declaration, so a 43284-first
                 * profile emitted a 43499 document - the A301SO complaint). */
                combination = if (state.combinationDerivedFor == config.release) {
                    /* Same profile: an explicit pick is kept. */
                    state.combination
                } else {
                    /* Profile CHANGED: re-derive from its declaration. The
                     * catalogue default only covers a profile that declares
                     * nothing at all (HISTORY: switching used to keep the old
                     * value, so a general profile still showed the 43499
                     * default - the A301SO locked-display complaint). */
                    declaredDefaultCombination(config.declaredCombinations)
                        ?: CombinationCatalog.defaultSpec
                },
                combinationDerivedFor = config.release,
                /* A profile that DECLARES combinations yet yields no usable
                 * default is reported, never silently papered over. */
                executionComboUnusable = state.combinationDerivedFor != config.release &&
                    config.declaredCombinations.isNotEmpty() &&
                    declaredDefaultCombination(config.declaredCombinations) == null,
                executionFields = config.general,
                executionEditing = if (preserveEditing) state.executionEditing
                else config.general.associate { field -> field.path to field.value.toString() },
                profileInvalidPaths = config.invalidPaths,
                profileRoute = config.route,
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
                aboutVisible = false,
                parametersVisible = false,
                loadConfigVisible = false,
                builtinScreenVisible = false,
                profileOverrideVisible = false,
                advancedOverrideVisible = false,
                pluginsScreenVisible = false,
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
                aboutVisible = false,
                parametersVisible = false,
                loadConfigVisible = false,
                builtinScreenVisible = false,
                profileOverrideVisible = false,
                advancedOverrideVisible = false,
                pluginsScreenVisible = false,
            )
        }
    }

    fun onOpenParameters() {
        mutableState.update {
            it.copy(
                advancedScreenVisible = true,
                parametersVisible = true,
                loadConfigVisible = false,
                builtinScreenVisible = false,
                profileOverrideVisible = false,
                advancedOverrideVisible = false,
                pluginsScreenVisible = false,
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

    /**
     * P1: opens the imported-plugin page. The registry comes from the
     * repository (no-backup store); the plugin schemas come from the native
     * probe, which is the P1 second half, so rows stay greyed until then.
     */
    /** P1: pick a plugin module; the probe runs after the pick. */
    fun onImportPlugin() {
        send(GhostlockEffect.PickDocument(DocumentRequest.PluginModule))
    }

    fun onOpenPlugins() {
        mutableState.update {
            it.copy(
                advancedScreenVisible = true,
                pluginsScreenVisible = true,
                aboutVisible = false,
                parametersVisible = false,
                loadConfigVisible = false,
                builtinScreenVisible = false,
                profileOverrideVisible = false,
                advancedOverrideVisible = false,
            )
        }
        refreshPlugins()
    }

    fun onClosePlugins() {
        mutableState.update { it.copy(pluginsScreenVisible = false) }
    }

    /**
     * Persists an enable/disable toggle and republishes the registry rows.
     *
     * It re-describes through [refreshPlugins] instead of projecting the rows
     * from the registry alone: the old version rebuilt them with NO descriptors,
     * so every row looked unusable and every enable switch greyed out — a
     * disabled plugin then could not be enabled again without leaving the page.
     * Re-describing also gives the row its schema back immediately.
     */
    fun onPluginEnabledChanged(id: String, enabled: Boolean) {
        viewModelScope.launch {
            runCatching { repository.setPluginEnabled(id, enabled) }.getOrNull()
                ?: return@launch
            refreshPlugins()
        }
    }

    private fun refreshPlugins() {
        viewModelScope.launch {
            val entries = runCatching { repository.pluginEntries() }.getOrElse { emptyList() }
            val importAvailable = runCatching { repository.pluginImportAvailable() }
                .getOrDefault(false)
            /* The probe re-describes every installed module against its pinned
             * digest, so the page renders the schema the module declares now. */
            val report = runCatching { repository.describePlugins() }.getOrNull()
            val descriptors = report?.descriptors.orEmpty()
            val describeFailures = report?.failures.orEmpty()
            /* The user's explicit overrides come from the controller's store;
             * the descriptor supplies the defaults and the types. */
            val overrides = runCatching { repository.pluginParamOverrides() }
                .getOrDefault(emptyMap())
            val extractValues = runCatching { repository.pluginExtractValues() }
                .getOrDefault(emptyMap())
            val detailId = state.value.pluginDetailId
            val detail = detailId?.let { id ->
                pluginDetailState(
                    id = id,
                    entries = entries,
                    descriptor = descriptors[id],
                    describeFailure = describeFailures[id],
                    overrides = overrides[id].orEmpty(),
                    extracts = extractValues[id].orEmpty(),
                    backend = state.value.backendKind,
                    selection = state.value.pluginRunSelection,
                )
            }
            mutableState.update {
                it.copy(
                    pluginRows = pluginRows(
                        entries,
                        descriptors,
                        it.backendKind,
                        it.pluginRunSelection,
                        describeFailures,
                    ),
                    pluginDescribeFailures = describeFailures,
                    pluginRunLogLine = PluginRunSelection.logLine(entries, it.pluginRunSelection),
                    pluginParams = descriptors.mapValues { (id, descriptor) ->
                        val entry = entries.firstOrNull { it.id == id }
                        val applied = overrides[id].orEmpty()
                        val errors = PluginConfigValidator.validate(
                            descriptor,
                            enabled = entry?.enabled == true,
                            stage = entry?.stage,
                            overrides = applied,
                        )
                        pluginParamRows(descriptor, applied, errors)
                    },
                    pluginImportEnabled = importAvailable,
                    pluginDetail = detail,
                )
            }
        }
    }

    /**
     * payload batch (a): the custom-execution draft. Transient UI state for now:
     * persistence lands with the wire owner (batch b), so nothing half-wired is
     * written into the profile yet. Any edit invalidates the confirmation.
     */
    fun onOpenPayload() {
        mutableState.update {
            it.copy(advancedScreenVisible = true, payloadVisible = true, pluginsScreenVisible = false)
        }
    }

    fun onClosePayload() {
        mutableState.update { it.copy(payloadVisible = false) }
    }

    fun onOpenExecutionCombination() {
        mutableState.update { it.copy(executionComboVisible = true) }
    }

    fun onCloseExecutionCombination() {
        mutableState.update { it.copy(executionComboVisible = false) }
    }

    fun onExecutionComboDraftChanged(entry: ExecutionComboEntry?) {
        mutableState.update { it.copy(executionComboDraft = entry, executionComboUnusable = false) }
    }

    /** Confirm path: applies the draft through the existing selection callback. */
    fun onExecutionComboConfirmed() {
        val draft = mutableState.value.executionComboDraft
        val spec = draft?.let { entry ->
            CombinationCatalog.specs.firstOrNull { option -> option.token == entry.token }
        }
        if (spec == null) {
            /* Never silent: the dialog reports why nothing was applied. */
            mutableState.update { it.copy(executionComboUnusable = true) }
            return
        }
        setCombination(spec)
        mutableState.update { it.copy(executionComboVisible = false, executionComboUnusable = false) }
    }

    /** `null` is the DEFAULT choice: nothing custom runs. */
    fun onPayloadTierChanged(tier: PayloadTier?) {
        mutableState.update { it.copy(payloadDraft = it.payloadDraft.copy(tier = tier)) }
    }

    fun onPayloadCommandChanged(command: String) {
        mutableState.update { it.copy(payloadDraft = it.payloadDraft.copy(execCommand = command)) }
    }

    /** The default tier's manager pick; `null` = the system default. */
    fun onPayloadManagerChanged(manager: RootManager?) {
        mutableState.update { it.copy(payloadDraft = it.payloadDraft.copy(rootManager = manager)) }
    }

    fun onPayloadPickScript() {
        send(GhostlockEffect.PickDocument(DocumentRequest.PayloadScript))
    }

    fun onPayloadPickKo() {
        send(GhostlockEffect.PickDocument(DocumentRequest.PayloadKo))
    }

    /** Copies the picked script; the page asks for no hash (user's ruling). */
    private fun onPayloadScriptPicked(uri: String, displayName: String?) {
        viewModelScope.launch {
            val result = runCatching { repository.importPayloadFile(PayloadKind.Script, uri, displayName) }
                .getOrElse { PayloadImportResult.Rejected(it.message ?: "cannot copy the script") }
            applyPayloadImport(result) { draft, imported ->
                draft.copy(
                    scriptName = imported.displayName,
                    scriptPath = imported.relativePath,
                )
            }
        }
    }

    /** Copies picked .ko modules, keeping the pick order as the load order. */
    private fun onPayloadKosPicked(uris: List<String>) {
        if (uris.isEmpty()) return
        viewModelScope.launch {
            var draft = state.value.payloadDraft
            for (uri in uris.take(PAYLOAD_MAX_KO - draft.koEntries.size)) {
                val result = runCatching {
                    repository.importPayloadFile(PayloadKind.Ko, uri, null)
                }.getOrElse { PayloadImportResult.Rejected(it.message ?: "cannot copy the module") }
                when (result) {
                    is PayloadImportResult.Imported -> {
                        draft = draft.copy(
                            koEntries = draft.koEntries + PayloadKoEntry(
                                name = result.displayName,
                                path = result.relativePath,
                            ),
                        )
                    }

                    is PayloadImportResult.Rejected -> {
                        send(GhostlockEffect.ToastArgs(R.string.payload_import_failed, result.reason))
                    }
                }
            }
            val updated = draft
            mutableState.update { it.copy(payloadDraft = updated) }
        }
    }

    fun onPayloadKoMove(index: Int, delta: Int) {
        mutableState.update {
            it.copy(
                payloadDraft = it.payloadDraft.copy(
                    koEntries = payloadKoMove(it.payloadDraft.koEntries, index, delta),
                ),
            )
        }
    }

    fun onPayloadKoRemove(index: Int) {
        mutableState.update {
            it.copy(
                payloadDraft = it.payloadDraft.copy(
                    koEntries = payloadKoRemove(it.payloadDraft.koEntries, index),
                ),
            )
        }
    }

    private fun applyPayloadImport(
        result: PayloadImportResult,
        apply: (PayloadDraft, PayloadImportResult.Imported) -> PayloadDraft,
    ) {
        when (result) {
            is PayloadImportResult.Imported -> mutableState.update {
                it.copy(payloadDraft = apply(it.payloadDraft, result))
            }

            is PayloadImportResult.Rejected -> send(
                GhostlockEffect.ToastArgs(R.string.payload_import_failed, result.reason),
            )
        }
    }

    /** batch ②: opens one plugin's detail page (data comes from the refresh). */
    fun onOpenPluginDetail(id: String) {
        mutableState.update { it.copy(pluginDetailId = id) }
        refreshPlugins()
    }

    fun onClosePluginDetail() {
        mutableState.update { it.copy(pluginDetailId = null, pluginDetail = null) }
    }

    /** Re-runs the probe for every installed module and rebuilds the report. */
    fun onRecheckPlugin(id: String) {
        mutableState.update { it.copy(pluginDetailId = id) }
        refreshPlugins()
    }

    fun onClearPluginOverrides(id: String) {
        viewModelScope.launch {
            runCatching { repository.clearPluginOverrides(id) }
            refreshPlugins()
        }
    }

    /** Everything the detail page shows, derived from one snapshot. */
    private suspend fun pluginDetailState(
        id: String,
        entries: List<PluginManifestEntry>,
        descriptor: PluginDescriptor?,
        describeFailure: String? = null,
        overrides: Map<String, PluginValue>,
        extracts: Map<String, PluginValue>,
        backend: BackendKind,
        selection: Set<String>?,
    ): PluginDetailState {
        val entry = entries.firstOrNull { it.id == id }
            ?: return PluginDetailState(
                id = id,
                issues = listOf(
                    PluginIssueRow(PluginIssueLevel.Error, R.string.plugin_issue_not_installed),
                ),
            )
        val selected = entry.enabled && (selection == null || id in selection)
        val installedPath = runCatching { repository.pluginInstalledPath(id) }.getOrNull() ?: "-"
        return PluginDetailState(
            id = id,
            header = pluginHeaderRows(entry, descriptor, installedPath, selected),
            params = descriptor?.let {
                pluginParamRows(
                    it,
                    overrides,
                    PluginConfigValidator.validate(it, entry.enabled, entry.stage, overrides),
                )
            }.orEmpty(),
            extracts = descriptor?.let { pluginExtractRows(it, extracts) }.orEmpty(),
            specs = descriptor?.let { pluginSpecRows(it, extracts) }.orEmpty(),
            issues = pluginIssueRows(
                descriptor = descriptor,
                entry = entry,
                describeFailure = describeFailure,
                overrides = overrides,
                extracts = extracts,
                selectedBackend = backend,
                selected = selected,
            ),
        )
    }

    /**
     * P1: edits one plugin parameter through the shared text dialog. The draft
     * is validated against the descriptor before it is stored; an empty draft
     * clears the override, so the descriptor default applies again.
     */
    fun onPluginParamEdit(id: String, name: String, current: String) {
        mutableState.update { pluginParamEditDialogState(it, id, name, current) }
    }

    /**
     * Batch 1: the run-level selection. Transient by design — it is pushed to
     * the repository (which composes the document) and reset after the run, so
     * the next run starts from the default "every enabled plugin".
     */
    fun onPluginRunSelected(id: String, selected: Boolean) {
        val current = state.value.pluginRows.filter { it.enabled }.map { it.id }.toSet()
        val next = if (selected) current + id else current - id
        applyPluginRunSelection(next)
    }

    fun onPluginRunSelectAll() = applyPluginRunSelection(null)

    fun onPluginRunSelectNone() = applyPluginRunSelection(emptySet())

    private fun applyPluginRunSelection(selection: Set<String>?) {
        repository.setPluginRunSelection(selection)
        mutableState.update { it.copy(pluginRunSelection = selection) }
        /* Re-project the rows from the registry, so the switch state is derived
         * from the repository's own view rather than from a second copy. */
        refreshPlugins()
    }

    /** P1: a bool parameter is stored straight from its switch. */
    fun onPluginBoolChanged(id: String, name: String, value: Boolean) {
        viewModelScope.launch {
            runCatching { repository.setPluginParam(id, name, PluginValue.Bool(value)) }
            refreshPlugins()
        }
    }

    private fun setPluginParam(id: String, name: String, text: String) {
        val type = state.value.pluginParams[id]?.firstOrNull { it.name == name }?.type
        if (type == null) {
            send(GhostlockEffect.Toast(R.string.plugin_param_invalid))
            return
        }
        val value = if (text.isBlank()) null else pluginValueFromText(type, text)
        if (text.isNotBlank() && value == null) {
            send(GhostlockEffect.Toast(R.string.plugin_param_invalid))
            return
        }
        viewModelScope.launch {
            runCatching { repository.setPluginParam(id, name, value) }
            refreshPlugins()
        }
    }

    /** Text -> typed value for the editor; null when the text is invalid. */
    private fun pluginValueFromText(type: PluginParamType, text: String): PluginValue? {
        val trimmed = text.trim()
        return when (type) {
            PluginParamType.UInt -> trimmed.toULongOrNull()?.let { PluginValue.UInt(it) }
            PluginParamType.Int -> trimmed.toLongOrNull()?.let { PluginValue.Int(it) }
            /* Bool is edited with a switch, so no text spelling is accepted. */
            PluginParamType.Bool -> null
            PluginParamType.Str -> PluginValue.Str(text)
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
                pluginsScreenVisible = false,
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
            val release = repository.editSessionRelease() ?: snapshot.kernelRelease
            val config = runCatching { session.load(release, pair) }.getOrNull()
                ?: return@launch
            applyExecutionConfig(config, preserveEditing)
            applyAdvancedConfig(config, preserveEditing)
        }
    }

    /** Writes the draft (general, route, advanced) into the session. */
    private suspend fun commitDraftToSession(): ProfileConfig? {
        val snapshot = kernelSnapshot ?: return null
        val pair = snapshot.cpuPairs.getOrNull(snapshot.selectedCpuPair) ?: return null
        val session = repository.editSessionController() ?: return null
        val release = repository.editSessionRelease() ?: snapshot.kernelRelease
        val general = mutableState.value.executionEditing.mapNotNull { (path, text) ->
            text.trim().toLongOrNull()?.let { value -> path to value }
        }.toMap()
        session.updateGeneral(release, pair, general)
        session.updateRoute(release, pair, mutableState.value.profileRoute)
        /* General edits share the execution.* tree paths. The advanced rebuild
         * replaces the whole override entry, so the drafts must be merged in or
         * the general edits would be dropped. */
        val drafts = mutableState.value.profileOverrideEditing +
                mutableState.value.executionEditing
        /* S4 R4: the 43284 policy paths are strings; every other draft is a
         * numeric leaf. The resolved tree is the authority for which leaves are
         * text (a materialised-but-empty policy path is covered explicitly). */
        val stringPaths = Cve2026_43284Fields.StringPaths.toSet() +
            flattenLeaves(mutableState.value.profileOverrideRoots)
                .mapNotNull { node -> node.textValue?.let { node.path } }
        val advanced = mutableMapOf<String, Any>()
        for ((path, text) in drafts) {
            if (path in stringPaths) {
                advanced[path] = text.trim()
            } else {
                text.trim().toLongOrNull()?.let { advanced[path] = it }
            }
        }
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
                    field.path to (field.value?.toString() ?: field.textValue ?: "")
                },
                profileInvalidPaths = config.invalidPaths,
                profileRoute = config.route,
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

    fun toggleForceAttackTest(enabled: Boolean) {
        repository.setForceAttackTest(enabled)
        mutableState.update { it.copy(forceAttackTestEnabled = enabled) }
    }

    /**
     * Compatibility entry point: maps the mode onto a catalogued combination,
     * preserving the selected route, and delegates to [setCombination] so the
     * single selection authority never desyncs (the W3 hint uses this to switch
     * to Shizuku). An off-catalogue mode keeps the legacy path.
     */
    fun setExecutionMode(mode: ExecutionMode) {
        val current = repository.currentCombination()
        val targetBackend = mode.backend
        val targetRoute = if (targetBackend == BackendKind.Cve2026_43499) current.route else null
        val target = CombinationCatalog.specs.firstOrNull {
            it.available && it.backend == targetBackend && it.steps == mode.steps &&
                it.route == targetRoute
        }
        if (target != null) {
            setCombination(target)
            return
        }
        repository.setExecutionMode(mode)
        mutableState.update { it.copy(executionMode = mode) }
        // The mode picks the backend/StepSet/terminal baked into the document,
        // so re-resolve the cached native blob before the next run.
        loadExecutionProfile(preserveEditing = true)
        // The grant dialog lands in another app, so the status is re-read and
        // onResume() refreshes it again when the dialog closes.
        viewModelScope.launch { refreshSnapshot() }
    }

    /** Compatibility entry point for the backend selector; delegates likewise. */
    fun setBackendKind(kind: BackendKind) {
        if (!kind.available) return
        val current = repository.currentCombination()
        val targetRoute = if (kind == BackendKind.Cve2026_43499) current.route else null
        val target = CombinationCatalog.specs.firstOrNull {
            it.available && it.backend == kind && it.steps == current.steps &&
                it.route == targetRoute
        } ?: CombinationCatalog.availableForBackend(kind).firstOrNull()
        if (target != null) {
            setCombination(target)
            return
        }
        repository.setBackendKind(kind)
        mutableState.update { it.copy(backendKind = kind) }
        // The built document carries the header id, so re-resolve it.
        loadExecutionProfile(preserveEditing = true)
    }

    /**
     * S4 R6b: the combination token is the single selection authority. The
     * derived backend/execution mode are kept in the state for the run button
     * and the activation card, and the document is re-resolved.
     */
    fun setCombination(spec: CombinationSpec) {
        if (!spec.available) return
        repository.setCombination(spec)
        mutableState.update {
            it.copy(
                combination = spec,
                backendKind = spec.backend,
                executionMode = spec.toExecutionMode(),
            )
        }
        loadExecutionProfile(preserveEditing = true)
        viewModelScope.launch { refreshSnapshot() }
    }

    fun onRun() {
        /* Batch 1: record what this run loads (the log travels with the run and
         * is exported; the selection itself never reaches the wire), then reset
         * to the default so the next run starts from "every enabled plugin". */
        val line = state.value.pluginRunLogLine
        if (line.isNotEmpty()) appendLog(line)
        /* payload ruling 9: the pre-run summary is evidence, so it is logged too. */
        val payloadDraft = state.value.payloadDraft
        if (payloadDraft.tier != null && payloadBlockers(payloadDraft).isEmpty()) {
            appendLog(payloadRunLogLine(payloadDraft))
        }
        runExploit()
        if (state.value.pluginRunSelection != null) {
            applyPluginRunSelection(null)
        }
    }

    /** Explains why the run button is greyed out. */
    fun onProfileInvalid() {
        val state = state.value
        /* (b1) A combination the profile does NOT declare blocks the run first:
         * the available declaration is the selection surface, so the refusal is
         * named and the selection is never silently replaced. */
        if (state.executionDeclarationErrors.isNotEmpty()) {
            send(
                GhostlockEffect.ToastArgs(
                    R.string.execution_declaration_blocked,
                    state.executionDeclarationErrors.first(),
                ),
            )
            return
        }
        /* A selected plugin that cannot be emitted blocks the document before any
         * of the generic reasons below apply: say WHY (the probe's own words). */
        if (state.executionPluginErrors.isNotEmpty()) {
            send(
                GhostlockEffect.ToastArgs(
                    R.string.plugin_run_blocked_plugin,
                    state.executionPluginErrors.first(),
                ),
            )
            return
        }
        val needsShell = runRequiresShizuku(state.executionMode, state.backendKind)
        val messageRes = when {
            !state.executionHasProfile -> R.string.run_blocked_no_profile
            needsShell && state.shizukuStatus != ShizukuStatus.READY ->
                R.string.run_blocked_shizuku

            else -> R.string.profile_invalid
        }
        send(GhostlockEffect.Toast(messageRes))
    }

    fun onStatusClick() {
        val snapshot = kernelSnapshot ?: return
        /* Shizuku is the shell route; the cve_2026_43284 backend always uses it. */
        if (!runRequiresShizuku(snapshot.executionMode, snapshot.backendKind)) return
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
        val mode = snapshot.executionMode
        if (!mode.isAvailable) {
            if (beginOperation()) {
                appendLog("result: execution mode ${mode.name} is not available")
                endOperation()
            }
            return
        }
        /* Fail closed on an unavailable backend (43284) even if a stale state
         * somehow carried it; the selector never offers it. */
        if (!snapshot.backendKind.available) {
            if (beginOperation()) {
                appendLog("result: backend ${snapshot.backendKind.token} is not available")
                endOperation()
            }
            return
        }
        if (runRequiresShizuku(mode, snapshot.backendKind) &&
            snapshot.shizukuStatus != ShizukuStatus.READY
        ) {
            onStatusClick()
            return
        }
        /* A custom tier that is not ready blocks the run and SAYS WHY, right
         * here: the page carries no check list any more. */
        payloadBlockers(state.value.payloadDraft).firstOrNull()?.let { blocker ->
            appendLog("result: custom execution is not ready")
            send(
                GhostlockEffect.ToastArgs(
                    blocker.resId,
                    blocker.args.firstOrNull()?.toString().orEmpty(),
                ),
            )
            return
        }
        val pair = snapshot.cpuPairs.getOrNull(snapshot.selectedCpuPair) ?: return
        if (!beginOperation()) return
        send(GhostlockEffect.KeepScreenAwake(true))
        /* The mode + backend derive the catalogued triple; log what it resolved. */
        val selection = resolveExecutionSelection(mode, snapshot.backendKind)
        /* The manager this run leaves behind (see RootManager; the package name
         * mirrors native `default_root_package`). */
        /* The user's pick on the custom-execution page wins over the run's own
         * default manager (same single table; see payloadLaunchManager). */
        val rootManagerPackage = payloadLaunchManager(state.value.payloadDraft)?.packageName
            ?: RootManager.of(selection.terminal)?.packageName
        /* force_attack_test ignores an already loaded KernelSU and discards the
         * root child: the run succeeds but leaves NO root state, so the manager
         * step is skipped rather than showing a UI that has nothing to show. */
        val rootProduced = !snapshot.forceAttackTest
        appendLog(
            "==== start ${if (mode == ExecutionMode.Shizuku) "Shizuku/V20" else "base"} " +
                "(backend=${selection.backend.token}, steps=${selection.steps.token}, " +
                "terminal=${selection.terminal.token}) ====",
        )
        appendLog("cpu pair: ${snapshot.cpuPairLabels.getOrElse(snapshot.selectedCpuPair) { pair.toString() }}")
        viewModelScope.launch(Dispatchers.IO) {
            try {
                val code = runExploitUseCase(pair, mode, snapshot.backendKind, ::appendLog)
                appendLog(if (code == 0) "result: exploit completed" else "result: exploit failed (exit code=$code)")
                appendLog("exit code=$code")
                /* Success is the App's existing criterion (exit code 0); the UI
                 * opens the root manager once, or says it cannot. An unfinished
                 * run never reaches this line. */
                send(
                    GhostlockEffect.ShowRootManager(
                        succeeded = code == 0,
                        rootProduced = rootProduced,
                        packageName = rootManagerPackage,
                    ),
                )
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
        parseDialogStage = ParseDialogStage.Mode
        mutableState.update {
            it.copy(
                dialogVisible = true,
                dialogType = DialogType.LIST,
                dialogTitleRes = R.string.parse_title,
                dialogItems = emptyList(),
                dialogItemResIds = listOf(
                    R.string.parse_option_payload,
                    R.string.parse_option_boot,
                ),
            )
        }
    }

    /** boot.img was chosen: let the user attach xbl_config / uefi (optional). */
    private fun promptBootAttach() {
        parseDialogStage = ParseDialogStage.Attach
        mutableState.update {
            it.copy(
                dialogVisible = true,
                dialogType = DialogType.LIST,
                dialogTitleRes = R.string.parse_boot_attach_title,
                dialogItems = emptyList(),
                dialogItemResIds = listOf(
                    R.string.parse_attach_none,
                    R.string.parse_attach_xbl,
                    R.string.parse_attach_uefi,
                    R.string.parse_attach_xbl_uefi,
                ),
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

    fun onDocumentResult(request: DocumentRequest, uri: String) {
        when (request) {
            DocumentRequest.PluginModule -> importPlugin(uri)
            DocumentRequest.PayloadScript -> onPayloadScriptPicked(uri, null)
            DocumentRequest.PayloadKo -> onPayloadKosPicked(listOf(uri))
            DocumentRequest.BootImage -> stageBoot(uri)
            DocumentRequest.XblImage -> stageXbl(uri)
            DocumentRequest.UefiImage -> stageUefi(uri)
            DocumentRequest.PayloadImage -> stagePayload(uri)
            DocumentRequest.ImportOffsetsHocon, DocumentRequest.ImportOffsetsJson -> Unit
        }
    }

    /**
     * P1 import: the repository stages, hashes and probes the picked module; the
     * page is refreshed with the probe result and the outcome is toasted.
     */
    private fun importPlugin(uri: String) {
        viewModelScope.launch {
            val result = runCatching {
                repository.importPlugin(uri, uri.substringAfterLast('/'))
            }.getOrElse { PluginImportResult.Rejected(it.message ?: "cannot import the plugin") }
            refreshPlugins()
            when (result) {
                is PluginImportResult.Imported -> send(
                    GhostlockEffect.Toast(R.string.plugin_imported),
                )

                is PluginImportResult.Rejected -> send(
                    GhostlockEffect.ToastArgs(R.string.plugin_import_failed, result.reason),
                )
            }
        }
    }

    /** Multi-picked documents (a profile plus any include dependencies). */
    fun onDocumentsResult(request: DocumentRequest, uris: List<String>) {
        when (request) {
            DocumentRequest.ImportOffsetsHocon, DocumentRequest.ImportOffsetsJson ->
                importDocuments(uris)

            DocumentRequest.PluginModule -> uris.firstOrNull()?.let(::importPlugin)
            DocumentRequest.BootImage -> uris.firstOrNull()?.let(::stageBoot)
            DocumentRequest.XblImage -> uris.firstOrNull()?.let(::stageXbl)
            DocumentRequest.UefiImage -> uris.firstOrNull()?.let(::stageUefi)
            DocumentRequest.PayloadImage -> uris.firstOrNull()?.let(::stagePayload)
            DocumentRequest.PayloadKo -> onPayloadKosPicked(uris)
            DocumentRequest.PayloadScript -> uris.firstOrNull()?.let { onPayloadScriptPicked(it, null) }
        }
    }

    fun onDialogItemSelected(index: Int) {
        dismissDialog()
        when (parseDialogStage) {
            ParseDialogStage.Mode -> when (index) {
                0 -> pickPayload()
                1 -> promptBootAttach()
            }

            ParseDialogStage.Attach -> when (index) {
                0 -> pickBoot(withXbl = false, withUefi = false)
                1 -> pickBoot(withXbl = true, withUefi = false)
                2 -> pickBoot(withXbl = false, withUefi = true)
                3 -> pickBoot(withXbl = true, withUefi = true)
            }
        }
    }

    fun onDialogInputChange(value: String) = mutableState.update { it.copy(dialogInput = value) }

    fun onDialogConfirm(value: String) {
        val dialogType = state.value.dialogType
        val renameTarget = state.value.userProfileRenameTarget
        val pluginId = state.value.pluginParamEditId
        val pluginName = state.value.pluginParamEditName
        dismissDialog(clearConfirmation = false)
        when {
            pluginId != null && pluginName != null -> setPluginParam(pluginId, pluginName, value)
            renameTarget != null -> renameUserProfile(renameTarget, value)
            dialogType == DialogType.INPUT -> parseUrl(value)
            dialogType == DialogType.CONFIRM -> setExecutionMode(ExecutionMode.Shizuku)
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
        /* Validate the resolved profile here so the run button can grey out. */
        val loaded = withContext(Dispatchers.IO) {
            val pair = snapshot.cpuPairs.getOrNull(snapshot.selectedCpuPair)
                ?: return@withContext null
            runCatching { profileController.load(snapshot.kernelRelease, pair) }.getOrNull()
        }
        kernelSnapshot = snapshot
        /* A cold start can reach the UI before the snapshot exists, and
         * loadExecutionProfile() used to drop such a request silently (it
         * returns when kernelSnapshot is null), which left the combination
         * chooser empty with no retry. Load once per release instead.
         * Idempotent: a second call for the same release is skipped.
         */
        if (loadedForRelease != snapshot.kernelRelease) {
            loadedForRelease = snapshot.kernelRelease
            loadExecutionProfile()
        }
        mutableState.update {
            it.copy(
                executionPluginErrors = loaded?.pluginErrors.orEmpty(),
                deviceName = snapshot.deviceName,
                kernelRelease = snapshot.kernelRelease,
                socName = snapshot.socName,
                kernelSupported = snapshot.kernelSupported,
                cpuPairLabels = snapshot.cpuPairLabels,
                cpuPairIndex = snapshot.selectedCpuPair,
                safeModeEnabled = snapshot.safeModeEnabled,
                forceAttackTestEnabled = snapshot.forceAttackTest,
                executionMode = snapshot.executionMode,
                backendKind = snapshot.backendKind,
                combination = repository.currentCombination(),
                shizukuStatus = snapshot.shizukuStatus,
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
                showNotice(R.string.import_result_title, R.string.import_failed)
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
                showNotice(
                    R.string.import_result_title,
                    if (matchesDevice) R.string.import_success else R.string.import_no_match,
                )
            }

            OffsetImportResult.AlreadyPresent -> {
                appendLog("result: offsets already present")
                showNotice(R.string.import_result_title, R.string.offsets_already_exist)
            }

            is OffsetImportResult.MissingIncludes -> {
                appendLog("import offsets missing includes: ${result.files.joinToString()}")
                appendLog("result: import failed")
                showNotice(R.string.import_result_title, R.string.import_missing_includes)
            }

            is OffsetImportResult.Failed -> {
                appendLog("import offsets failed: ${result.reason}")
                appendLog("result: import failed")
                showNotice(R.string.import_result_title, R.string.import_failed)
            }
        }
    }

    private fun pickPayload() {
        pendingParseWithXbl = false
        pendingParseWithUefi = false
        send(GhostlockEffect.Toast(R.string.parse_pick_payload_hint))
        send(GhostlockEffect.PickDocument(DocumentRequest.PayloadImage))
    }

    private fun pickBoot(withXbl: Boolean, withUefi: Boolean) {
        pendingParseWithXbl = withXbl
        pendingParseWithUefi = withUefi
        pendingXblPath = null
        pendingUefiPath = null
        if (withXbl) send(GhostlockEffect.Toast(R.string.parse_pick_boot_hint))
        send(GhostlockEffect.PickDocument(DocumentRequest.BootImage))
    }

    private fun stageBoot(uri: String) {
        viewModelScope.launch(Dispatchers.IO) {
            try {
                val bootPath = readDocumentUseCase.cache(uri, "boot.img")
                pendingBootPath = bootPath
                appendLog("boot.img ready: $bootPath")
                when {
                    pendingParseWithXbl -> {
                        send(GhostlockEffect.Toast(R.string.parse_pick_xbl_hint))
                        send(GhostlockEffect.PickDocument(DocumentRequest.XblImage))
                    }

                    pendingParseWithUefi -> {
                        send(GhostlockEffect.Toast(R.string.parse_pick_uefi_hint))
                        send(GhostlockEffect.PickDocument(DocumentRequest.UefiImage))
                    }

                    else -> runParse(bootPath)
                }
            } catch (error: CancellationException) {
                throw error
            } catch (error: Exception) {
                appendLog("parse error: ${error.message}")
                appendLog("result: parse failed")
                showNotice(R.string.parse_result_title, R.string.parse_failed)
            }
        }
    }

    private fun stageXbl(uri: String) {
        viewModelScope.launch(Dispatchers.IO) {
            try {
                val bootPath = requireNotNull(pendingBootPath) { "boot.img is not staged" }
                val xblPath = readDocumentUseCase.cache(uri, "xbl_config.img")
                pendingXblPath = xblPath
                appendLog("xbl_config.img ready: $xblPath")
                if (pendingParseWithUefi) {
                    send(GhostlockEffect.Toast(R.string.parse_pick_uefi_hint))
                    send(GhostlockEffect.PickDocument(DocumentRequest.UefiImage))
                } else {
                    runParse(bootPath, xblPath = xblPath)
                }
            } catch (error: CancellationException) {
                throw error
            } catch (error: Exception) {
                appendLog("parse error: ${error.message}")
                appendLog("result: parse failed")
                showNotice(R.string.parse_result_title, R.string.parse_failed)
            }
        }
    }

    private fun stageUefi(uri: String) {
        viewModelScope.launch(Dispatchers.IO) {
            try {
                val bootPath = requireNotNull(pendingBootPath) { "boot.img is not staged" }
                val uefiPath = readDocumentUseCase.cache(uri, "uefi.img")
                pendingUefiPath = uefiPath
                appendLog("uefi.img ready: $uefiPath")
                runParse(bootPath, xblPath = pendingXblPath, uefiPath = uefiPath)
            } catch (error: CancellationException) {
                throw error
            } catch (error: Exception) {
                appendLog("parse error: ${error.message}")
                appendLog("result: parse failed")
                showNotice(R.string.parse_result_title, R.string.parse_failed)
            }
        }
    }

    private fun stagePayload(uri: String) {
        viewModelScope.launch(Dispatchers.IO) {
            try {
                val payloadPath = readDocumentUseCase.cache(uri, "payload.bin")
                appendLog("payload.bin ready: $payloadPath")
                runParse(payloadPath)
            } catch (error: CancellationException) {
                throw error
            } catch (error: Exception) {
                appendLog("parse error: ${error.message}")
                appendLog("result: parse failed")
                showNotice(R.string.parse_result_title, R.string.parse_failed)
            }
        }
    }

    private fun parseUrl(value: String) {
        val url = value.trim()
        if (url.isEmpty() || !(url.startsWith("http://") || url.startsWith("https://"))) {
            appendLog("error: invalid OTA URL: $url")
            appendLog("result: parse failed")
            showNotice(R.string.parse_result_title, R.string.parse_failed_url)
            return
        }
        appendLog("parse OTA: $url")
        viewModelScope.launch(Dispatchers.IO) { runParse(url) }
    }

    /** Confirm/notice popup for an extractor outcome; unlike a Toast it waits
     * for the user and can link the matching documentation page. */
    private fun showNotice(titleRes: Int, messageRes: Int, docUrl: String? = null) {
        mutableState.update {
            it.copy(
                dialogVisible = true,
                dialogType = DialogType.NOTICE,
                dialogTitleRes = titleRes,
                dialogMessageRes = messageRes,
                dialogDocUrl = docUrl,
                dialogConfirmLabelRes = R.string.dialog_dismiss,
            )
        }
    }

    private fun isMediaTek(): Boolean {
        val soc = kernelSnapshot?.socName?.lowercase(Locale.ROOT).orEmpty()
        return soc.contains("mediatek") || soc.contains("mtk") ||
                soc.contains("dimensity") || soc.contains("helio")
    }

    private fun mediatekDocUrl(): String =
        "https://github.com/YuKongA/ghostlock-app/blob/main/docs/profile/" +
                if (Locale.getDefault().language == "zh") "MEDIATEK_ZH.md" else "MEDIATEK.md"

    private suspend fun runParse(
        input: String,
        xblPath: String? = null,
        uefiPath: String? = null,
        overwrite: Boolean = false,
    ) {
        if (!beginOperation()) return
        try {
            when (val result = parseSourceUseCase(input, xblPath, uefiPath, overwrite, ::appendLog)) {
                is ParseResult.RequiresOverwrite -> {
                    pendingConfirmation = PendingConfirmation.Parse(input, xblPath, uefiPath)
                    showOverwriteDialog(result.releases)
                }

                is ParseResult.Parsed -> {
                    refreshSnapshot()
                    refreshUserProfiles()
                    appendLog("offsets exported: ${result.releases.joinToString()}")
                    appendLog("result: offsets parsed successfully")
                    /* Auto-load the just-saved document so the parsed profile
                     * takes effect without a manual trip to the profile list. */
                    if (result.documentName != null) {
                        appendLog("auto-loading parsed profile: ${result.documentName}")
                        selectUserProfile(result.documentName)
                    }
                    if (result.missing.isNotEmpty()) {
                        appendLog(
                            "warning: missing ${result.missing.joinToString()}; " +
                                    "run the MediaTek extractor or attach xbl_config.img / uefi.img",
                        )
                        val mediaTek = isMediaTek()
                        showNotice(
                            titleRes = R.string.parse_result_title,
                            messageRes = if (mediaTek) {
                                R.string.parse_missing_phys_mediatek
                            } else {
                                R.string.parse_missing_phys_hint
                            },
                            docUrl = if (mediaTek) mediatekDocUrl() else null,
                        )
                    } else if (result.documentName == null) {
                        showNotice(R.string.parse_result_title, R.string.parse_success)
                    }
                }

                ParseResult.AlreadyPresent -> {
                    appendLog("result: offsets already present")
                    showNotice(R.string.parse_result_title, R.string.offsets_already_exist)
                }

                is ParseResult.Failed -> {
                    result.reason?.let { appendLog("parse failed: $it") }
                    appendLog("result: ${parseFailureResult(result.code)}")
                    showNotice(R.string.parse_result_title, parseFailureToast(result.code))
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
                runParse(
                    confirmation.input,
                    confirmation.xblPath,
                    confirmation.uefiPath,
                    overwrite = true,
                )
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
                dialogTitleRes = null,
                dialogMessage = "",
                dialogMessageRes = null,
                dialogItems = emptyList(),
                dialogItemResIds = emptyList(),
                dialogCurrentItemIndex = -1,
                dialogInput = "",
                dialogConfirmLabelRes = R.string.parse_start,
                dialogDocUrl = null,
                userProfileRenameTarget = null,
                pluginParamEditId = null,
                pluginParamEditName = null,
            )
        }
    }

    private fun appendLog(line: String) {
        /* The final result lines are Kotlin-side; tag them so they read as <k>. */
        val tagged = if (line.startsWith("result:")) "<k> $line" else line
        val entry = formatLog(tagged)
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
        LogTone.Kotlin -> 0xFF5EEAD4.toInt()
        LogTone.Shizuku -> 0xFFC084FC.toInt()
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
        data class Parse(val input: String, val xblPath: String?, val uefiPath: String?) : PendingConfirmation
    }
}
