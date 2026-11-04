package com.ghostlock.app

import android.content.Context
import androidx.lifecycle.ViewModelProvider
import com.ghostlock.app.backend.declarations.BackendRegistryIdentityFactory
import com.ghostlock.app.backend.registry.Backends
import com.ghostlock.app.backend.launcher.RunCoordinator
import com.ghostlock.app.data.backend.BackendCatalogAdapter
import com.ghostlock.app.data.backend.BackendPresentationAdapter
import com.ghostlock.app.data.backend.BackendRunSessionAdapter
import com.ghostlock.app.data.backend.BackendSelectionAdapter
import com.ghostlock.app.data.backend.BackendSessionStore
import com.ghostlock.app.data.backend.AndroidBackendConfigurationAdapter
import com.ghostlock.app.data.device.AndroidDeviceStatusAdapter
import com.ghostlock.app.data.diagnostics.AndroidDiagnosticReportBuilder
import com.ghostlock.app.data.finish.FinishSettingsAdapter
import com.ghostlock.app.data.finish.FinishEditorAdapter
import com.ghostlock.app.data.privilege.AndroidPrivilegeAdapter
import com.ghostlock.app.data.settings.SharedPreferencesUiPreferencesRepository
import com.ghostlock.app.data.settings.SharedPreferencesOperationalPreferencesAdapter
import com.ghostlock.app.data.update.DisabledAppUpdateAdapter
import com.ghostlock.app.domain.backend.GetBackendCatalogUseCase
import com.ghostlock.app.domain.backend.SelectBackendUseCase
import com.ghostlock.app.domain.run.RunBackendUseCase
import com.ghostlock.app.domain.device.ReadDeviceStatusUseCase
import com.ghostlock.app.domain.diagnostics.CreateDiagnosticReportUseCase
import com.ghostlock.app.domain.finish.ReadFinishSettingsUseCase
import com.ghostlock.app.domain.finish.SaveFinishSettingsUseCase
import com.ghostlock.app.domain.finish.PreviewFinishScriptUseCase
import com.ghostlock.app.domain.finish.ReadFinishEditorUseCase
import com.ghostlock.app.domain.finish.SaveFinishEditorUseCase
import com.ghostlock.app.domain.privilege.ObservePrivilegeUseCase
import com.ghostlock.app.domain.privilege.RefreshPrivilegeUseCase
import com.ghostlock.app.domain.privilege.SelectPrivilegeExecutorUseCase
import com.ghostlock.app.domain.settings.ObserveUiPreferencesUseCase
import com.ghostlock.app.domain.settings.ImportTerminalFontUseCase
import com.ghostlock.app.domain.settings.ObserveOperationalPreferencesUseCase
import com.ghostlock.app.domain.settings.SaveUiPreferencesUseCase
import com.ghostlock.app.domain.settings.SaveOperationalPreferencesUseCase
import com.ghostlock.app.domain.settings.SelectLogExportDirectoryUseCase
import com.ghostlock.app.domain.settings.SetNavigationRailExpandedUseCase
import com.ghostlock.app.domain.update.CheckAppUpdateUseCase
import com.ghostlock.app.ui.backend.BackendConfigRenderer
import com.ghostlock.app.ui.backend.RegisteredBackendConfigRenderer

/**
 * Application-scoped composition root for Clean Architecture adapters and use cases.
 *
 * @param context Application context used to construct Android data adapters once.
 * @property viewModelFactory Factory that injects the settings and device-domain use cases.
 * @note Physical adapter lifetime equals the application process; presentation never constructs it directly.
 */
class AppContainer(context: Context) {
    private val applicationContext = context.applicationContext
    private val backendRegistryEntries = Backends.all.toList()
    private val backendRegistryIdentity = BackendRegistryIdentityFactory.capture(
        backendRegistryEntries,
    )
    private val backendSessionStore = BackendSessionStore.from(context)
    private val backendConfigurationAdapter = AndroidBackendConfigurationAdapter(
        context = context,
        registryEntries = backendRegistryEntries,
        sessionStore = backendSessionStore,
    )
    private val uiPreferencesRepository = SharedPreferencesUiPreferencesRepository(context)
    private val operationalPreferencesAdapter = SharedPreferencesOperationalPreferencesAdapter(context)
    private val observeUiPreferences = ObserveUiPreferencesUseCase(uiPreferencesRepository)
    private val observeOperationalPreferences = ObserveOperationalPreferencesUseCase(
        operationalPreferencesAdapter,
    )
    private val saveUiPreferences = SaveUiPreferencesUseCase(uiPreferencesRepository)
    private val saveOperationalPreferences = SaveOperationalPreferencesUseCase(
        operationalPreferencesAdapter,
    )
    private val importTerminalFont = ImportTerminalFontUseCase(operationalPreferencesAdapter)
    private val selectLogExportDirectory = SelectLogExportDirectoryUseCase(
        operationalPreferencesAdapter,
    )
    private val setNavigationRailExpanded = SetNavigationRailExpandedUseCase(uiPreferencesRepository)
    private val privilegeAdapter = AndroidPrivilegeAdapter(context)
    private val observePrivilege = ObservePrivilegeUseCase(privilegeAdapter)
    private val refreshPrivilege = RefreshPrivilegeUseCase(privilegeAdapter)
    private val selectPrivilegeExecutor = SelectPrivilegeExecutorUseCase(privilegeAdapter)
    private val deviceStatusAdapter = AndroidDeviceStatusAdapter()
    private val readDeviceStatus = ReadDeviceStatusUseCase(deviceStatusAdapter)
    private val checkAppUpdate = CheckAppUpdateUseCase(DisabledAppUpdateAdapter())
    private val backendCatalogAdapter = BackendCatalogAdapter(
        registryEntries = backendRegistryEntries,
        identity = backendRegistryIdentity,
        selectedKey = { backendSessionStore.selectedKey },
        presentation = BackendPresentationAdapter(context),
    )
    private val getBackendCatalog = GetBackendCatalogUseCase(backendCatalogAdapter)
    private val selectBackend = SelectBackendUseCase(
        BackendSelectionAdapter(context, backendRegistryEntries, backendSessionStore),
    )
    private val runCoordinator = RunCoordinator()
    private val runBackend = RunBackendUseCase(
        BackendRunSessionAdapter(
            context = context,
            registryEntries = backendRegistryEntries,
            selectedKey = { backendSessionStore.selectedKey },
            coordinator = runCoordinator,
            observeOperationalPreferences = observeOperationalPreferences,
            observePrivilege = observePrivilege,
            registryIdentity = backendRegistryIdentity,
        ),
    )
    private val diagnosticReportAdapter = AndroidDiagnosticReportBuilder(context)
    private val finishSettingsAdapter = FinishSettingsAdapter(context)
    private val readFinishSettings = ReadFinishSettingsUseCase(finishSettingsAdapter)
    private val saveFinishSettings = SaveFinishSettingsUseCase(finishSettingsAdapter)
    private val finishEditorAdapter = FinishEditorAdapter(context)
    private val readFinishEditor = ReadFinishEditorUseCase(finishEditorAdapter)
    private val saveFinishEditor = SaveFinishEditorUseCase(finishEditorAdapter)
    private val previewFinishScript = PreviewFinishScriptUseCase(finishEditorAdapter)

    /** Application diagnostic report command used by presentation save/share actions. */
    val createDiagnosticReport = CreateDiagnosticReportUseCase(diagnosticReportAdapter)

    /** Restores the captured backend selection and configuration before the first composition. */
    fun restoreBackendSession() {
        backendSessionStore.restore(applicationContext, backendRegistryEntries)
        backendRegistryEntries.forEach { spec -> backendConfigurationAdapter.refresh(spec.key) }
    }

    /** Presentation-owned configuration renderer over the same captured registry membership. */
    val backendConfigRenderer: BackendConfigRenderer =
        RegisteredBackendConfigRenderer(backendConfigurationAdapter)

    val viewModelFactory: ViewModelProvider.Factory = AppViewModelFactory(
        observeUiPreferences = observeUiPreferences,
        observeOperationalPreferences = observeOperationalPreferences,
        saveUiPreferences = saveUiPreferences,
        saveOperationalPreferences = saveOperationalPreferences,
        importTerminalFont = importTerminalFont,
        selectLogExportDirectory = selectLogExportDirectory,
        setNavigationRailExpanded = setNavigationRailExpanded,
        readDeviceStatus = readDeviceStatus,
        getBackendCatalog = getBackendCatalog,
        selectBackend = selectBackend,
        checkAppUpdate = checkAppUpdate,
        observePrivilege = observePrivilege,
        refreshPrivilege = refreshPrivilege,
        selectPrivilegeExecutor = selectPrivilegeExecutor,
        readFinishSettings = readFinishSettings,
        saveFinishSettings = saveFinishSettings,
        readFinishEditor = readFinishEditor,
        saveFinishEditor = saveFinishEditor,
        previewFinishScript = previewFinishScript,
        runBackend = runBackend,
        appVersionName = BuildConfig.VERSION_NAME,
        appVersionCode = BuildConfig.VERSION_CODE.toLong(),
        isPrBuild = BuildConfig.IS_PR_BUILD,
    )
}
