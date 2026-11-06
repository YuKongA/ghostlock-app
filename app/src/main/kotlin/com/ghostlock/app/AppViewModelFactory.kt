package com.ghostlock.app

import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.createSavedStateHandle
import androidx.lifecycle.viewmodel.CreationExtras
import com.ghostlock.app.domain.backend.GetBackendCatalogUseCase
import com.ghostlock.app.domain.backend.SelectBackendUseCase
import com.ghostlock.app.domain.device.ReadDeviceStatusUseCase
import com.ghostlock.app.domain.finish.ReadFinishSettingsUseCase
import com.ghostlock.app.domain.finish.SaveFinishSettingsUseCase
import com.ghostlock.app.domain.payload.ReadPayloadEditorUseCase
import com.ghostlock.app.domain.payload.SavePayloadEditorUseCase
import com.ghostlock.app.domain.run.PreviewRootScriptUseCase
import com.ghostlock.app.domain.privilege.ObservePrivilegeUseCase
import com.ghostlock.app.domain.privilege.RefreshPrivilegeUseCase
import com.ghostlock.app.domain.privilege.SelectPrivilegeExecutorUseCase
import com.ghostlock.app.domain.run.RunBackendUseCase
import com.ghostlock.app.domain.settings.ObserveUiPreferencesUseCase
import com.ghostlock.app.domain.settings.ImportTerminalFontUseCase
import com.ghostlock.app.domain.settings.ObserveOperationalPreferencesUseCase
import com.ghostlock.app.domain.settings.SaveUiPreferencesUseCase
import com.ghostlock.app.domain.settings.SaveOperationalPreferencesUseCase
import com.ghostlock.app.domain.settings.SelectLogExportDirectoryUseCase
import com.ghostlock.app.domain.settings.SetNavigationRailExpandedUseCase
import com.ghostlock.app.domain.update.CheckAppUpdateUseCase
import com.ghostlock.app.ui.viewmodel.HomeViewModel
import com.ghostlock.app.ui.viewmodel.FinishSettingsViewModel
import com.ghostlock.app.ui.viewmodel.PayloadViewModel
import com.ghostlock.app.ui.viewmodel.MainActivityViewModel
import com.ghostlock.app.ui.viewmodel.PathViewModel
import com.ghostlock.app.ui.viewmodel.SettingsViewModel
import com.ghostlock.app.ui.viewmodel.RunViewModel

/**
 * Explicit composition factory for application-shell view models.
 *
 * @param observeUiPreferences Domain use case injected into MainActivityViewModel.
 * @param setNavigationRailExpanded Domain command injected into MainActivityViewModel.
 * @param readDeviceStatus Domain use case injected into HomeViewModel.
 * @note Unknown model classes fail by name instead of silently constructing a presentation dependency.
 */
class AppViewModelFactory(
    private val observeUiPreferences: ObserveUiPreferencesUseCase,
    private val observeOperationalPreferences: ObserveOperationalPreferencesUseCase,
    private val saveUiPreferences: SaveUiPreferencesUseCase,
    private val saveOperationalPreferences: SaveOperationalPreferencesUseCase,
    private val importTerminalFont: ImportTerminalFontUseCase,
    private val selectLogExportDirectory: SelectLogExportDirectoryUseCase,
    private val setNavigationRailExpanded: SetNavigationRailExpandedUseCase,
    private val readDeviceStatus: ReadDeviceStatusUseCase,
    private val getBackendCatalog: GetBackendCatalogUseCase,
    private val selectBackend: SelectBackendUseCase,
    private val checkAppUpdate: CheckAppUpdateUseCase,
    private val observePrivilege: ObservePrivilegeUseCase,
    private val refreshPrivilege: RefreshPrivilegeUseCase,
    private val selectPrivilegeExecutor: SelectPrivilegeExecutorUseCase,
    private val readFinishSettings: ReadFinishSettingsUseCase,
    private val saveFinishSettings: SaveFinishSettingsUseCase,
    private val readPayloadEditor: ReadPayloadEditorUseCase,
    private val savePayloadEditor: SavePayloadEditorUseCase,
    private val previewRootScript: PreviewRootScriptUseCase,
    private val runBackend: RunBackendUseCase,
    private val appVersionName: String,
    private val appVersionCode: Long,
    private val isPrBuild: Boolean,
) : ViewModelProvider.Factory {
    /**
     * Creates the requested application-shell view model from lifecycle creation extras.
     *
     * @param modelClass Requested ViewModel class.
     * @param extras Saved-state ownership supplied by the Activity.
     * @return MainActivityViewModel with its domain dependency injected.
     * @throws IllegalArgumentException When this factory receives an unregistered model class.
     */
    @Suppress("UNCHECKED_CAST")
    override fun <T : ViewModel> create(modelClass: Class<T>, extras: CreationExtras): T {
        if (modelClass == MainActivityViewModel::class.java) {
            return MainActivityViewModel(
                savedStateHandle = extras.createSavedStateHandle(),
                observeUiPreferences = observeUiPreferences,
                setNavigationRailExpanded = setNavigationRailExpanded,
            ) as T
        }
        if (modelClass == HomeViewModel::class.java) {
            return HomeViewModel(
                readDeviceStatus = readDeviceStatus,
                getBackendCatalog = getBackendCatalog,
                observeUiPreferences = observeUiPreferences,
                checkAppUpdate = checkAppUpdate,
                observePrivilege = observePrivilege,
                refreshPrivilege = refreshPrivilege,
                selectPrivilegeExecutor = selectPrivilegeExecutor,
                appVersionName = appVersionName,
                appVersionCode = appVersionCode,
                isPrBuild = isPrBuild,
            ) as T
        }
        if (modelClass == SettingsViewModel::class.java) {
            return SettingsViewModel(
                observeUiPreferences = observeUiPreferences,
                saveUiPreferences = saveUiPreferences,
                observeOperationalPreferences = observeOperationalPreferences,
                saveOperationalPreferences = saveOperationalPreferences,
                importTerminalFont = importTerminalFont,
                selectLogExportDirectoryUseCase = selectLogExportDirectory,
            ) as T
        }
        if (modelClass == PathViewModel::class.java) {
            return PathViewModel(
                getBackendCatalog = getBackendCatalog,
                selectBackend = selectBackend,
            ) as T
        }
        if (modelClass == FinishSettingsViewModel::class.java) {
            return FinishSettingsViewModel(
                readFinishSettings = readFinishSettings,
                saveFinishSettings = saveFinishSettings,
            ) as T
        }
        if (modelClass == PayloadViewModel::class.java) {
            return PayloadViewModel(
                readPayloadEditor = readPayloadEditor,
                savePayloadEditor = savePayloadEditor,
                previewRootScript = previewRootScript,
            ) as T
        }
        if (modelClass == RunViewModel::class.java) {
            return RunViewModel(runBackend = runBackend) as T
        }
        throw IllegalArgumentException("unregistered ViewModel: ${modelClass.name}")
    }
}
