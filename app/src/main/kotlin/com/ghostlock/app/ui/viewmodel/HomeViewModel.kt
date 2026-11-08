package com.ghostlock.app.ui.viewmodel

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.ghostlock.app.domain.device.ReadDeviceStatusUseCase
import com.ghostlock.app.domain.settings.ObserveUiPreferencesUseCase
import com.ghostlock.app.domain.privilege.AttackExecutorKind
import com.ghostlock.app.domain.privilege.ExecutorSelectionResult
import com.ghostlock.app.domain.privilege.ObservePrivilegeUseCase
import com.ghostlock.app.domain.privilege.PrivilegeState
import com.ghostlock.app.domain.privilege.RefreshPrivilegeUseCase
import com.ghostlock.app.domain.privilege.SelectPrivilegeExecutorUseCase
import com.ghostlock.app.domain.update.AppUpdateInfo
import com.ghostlock.app.domain.update.CheckAppUpdateUseCase
import com.ghostlock.app.ui.screen.home.HomeUiState
import com.ghostlock.app.ui.screen.home.PrivilegeUiState
import com.ghostlock.app.ui.screen.home.SelectedBackendUiState
import com.ghostlock.app.ui.screen.home.SystemInfo
import com.ghostlock.app.ui.backend.BackendUiCatalog
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/**
 * Builds renderer-neutral home state from injected device/settings facts and the backend registry.
 *
 * @param readDeviceStatus Device-status use case owned by AppContainer.
 * @param backendUiCatalog UI-owned projection of the sole backend definition snapshot.
 * @param observeUiPreferences Application-shell preference use case owned by AppContainer.
 * @param checkAppUpdate Application update use case owned by AppContainer.
 * @param observePrivilege Live executor and Shizuku facts owned by the application data adapter.
 * @param refreshPrivilege Explicit foreground refresh command for live privilege facts.
 * @param selectPrivilegeExecutor Executor selection and permission-request command.
 * @param appVersionName Build version label supplied by the composition root.
 * @param appVersionCode Build version code supplied by the composition root.
 * @param isPrBuild Whether this application build is a pull-request build.
 */
class HomeViewModel(
    private val readDeviceStatus: ReadDeviceStatusUseCase,
    private val backendUiCatalog: BackendUiCatalog,
    observeUiPreferences: ObserveUiPreferencesUseCase,
    private val checkAppUpdate: CheckAppUpdateUseCase,
    observePrivilege: ObservePrivilegeUseCase,
    private val refreshPrivilege: RefreshPrivilegeUseCase,
    private val selectPrivilegeExecutor: SelectPrivilegeExecutorUseCase,
    private val appVersionName: String,
    private val appVersionCode: Long,
    private val isPrBuild: Boolean,
) : ViewModel() {
    private val preferences = observeUiPreferences()
    private val privilege = observePrivilege()
    private val mutableUiState = MutableStateFlow(buildState())
    val uiState: StateFlow<HomeUiState> = mutableUiState.asStateFlow()

    init {
        viewModelScope.launch {
            privilege.collectLatest { state ->
                mutableUiState.update { current -> current.copy(privilege = state.toUiState()) }
            }
        }
    }

    /** Refreshes device/backend facts and optionally performs the configured update check. */
    fun refresh() {
        mutableUiState.update { current ->
            current.copy(selectedBackend = selectedBackendState())
        }
        refreshPrivilege()
        viewModelScope.launch {
            val baseState = withContext(Dispatchers.IO) { buildState() }
            mutableUiState.value = baseState
            if (baseState.checkUpdateEnabled) {
                val latestVersionInfo = withContext(Dispatchers.IO) { checkAppUpdate() }
                mutableUiState.update { it.copy(latestVersionInfo = latestVersionInfo) }
            }
        }
    }

    /**
     * Handles one explicit executor choice without exposing storage or Shizuku APIs to presentation.
     *
     * @param kind Requested executor family.
     * @return Exact outcome used to keep the overview or show the Shizuku help page.
     */
    fun selectExecutor(
        kind: AttackExecutorKind,
        onResult: (ExecutorSelectionResult) -> Unit,
    ) {
        viewModelScope.launch {
            onResult(selectPrivilegeExecutor(kind))
        }
    }

    /** @return One current state snapshot without Android or legacy manager presentation globals. */
    private fun buildState(): HomeUiState {
        val deviceStatus = readDeviceStatus()
        return HomeUiState(
            selectedBackend = selectedBackendState(),
            isPrBuild = isPrBuild,
            checkUpdateEnabled = preferences.value.checkUpdate,
            latestVersionInfo = AppUpdateInfo(),
            currentAppVersionCode = appVersionCode,
            systemInfo = SystemInfo(
                kernelVersion = deviceStatus.kernelRelease,
                appVersion = "$appVersionName ($appVersionCode)",
                deviceModel = deviceStatus.deviceModel,
                fingerprint = deviceStatus.fingerprint,
                selinuxStatus = deviceStatus.selinuxStatus,
                seccompStatus = deviceStatus.seccompStatus,
            ),
            privilege = privilege.value.toUiState(),
        )
    }

    /** @return Current selected-backend readiness projected without waiting for device I/O. */
    private fun selectedBackendState(): SelectedBackendUiState {
        val selectedBackend = backendUiCatalog.selectedEntry() ?: return SelectedBackendUiState(
            cveLabel = "",
            statusText = "No backend is available.",
            isReady = false,
        )
        val descriptor = selectedBackend.descriptor
        val availability = backendUiCatalog.availabilityFor(descriptor.key)
        val ready = descriptor.selectable && availability.isReady
        val status = when {
            ready -> "Ready"
            availability.reasons.isNotEmpty() -> availability.reasons.first().message
            else -> "The selected backend is disabled."
        }
        return SelectedBackendUiState(
            cveLabel = backendUiCatalog.categoryLabel(selectedBackend),
            statusText = status,
            isReady = ready,
        )
    }

    /** @return Presentation projection without Android or component-specific types. */
    private fun PrivilegeState.toUiState(): PrivilegeUiState = PrivilegeUiState(
        selectedExecutor = selectedExecutor,
        displayedExecutor = displayedExecutor(),
        appUid = appUid,
        shizukuAvailable = shizukuAvailable,
        shizukuGranted = shizukuGranted,
    )
}
