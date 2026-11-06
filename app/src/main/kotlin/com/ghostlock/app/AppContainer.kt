package com.ghostlock.app

import android.content.Context
import androidx.lifecycle.ViewModelProvider
import com.ghostlock.app.backend.registry.BackendRegistry
import com.ghostlock.app.backend.transport.AppRunHostClient
import com.ghostlock.app.backend.transport.ShizukuRunHostClient
import com.ghostlock.app.core.run.RunCoordinator
import com.ghostlock.app.core.run.RunInputFreezer
import com.ghostlock.app.data.backend.BackendCatalogAdapter
import com.ghostlock.app.data.backend.BackendRunSessionAdapter
import com.ghostlock.app.data.backend.BackendSelectionAdapter
import com.ghostlock.app.data.backend.BackendSessionStore
import com.ghostlock.app.data.backend.AndroidBackendConfigurationAdapter
import com.ghostlock.app.data.device.AndroidDeviceStatusAdapter
import com.ghostlock.app.data.diagnostics.AndroidDiagnosticReportBuilder
import com.ghostlock.app.data.finish.FinishDocumentRepository
import com.ghostlock.app.data.payload.PayloadDocumentRepository
import com.ghostlock.app.data.privilege.AndroidPrivilegeAdapter
import com.ghostlock.app.data.run.AndroidRunLogSinkFactory
import com.ghostlock.app.data.run.PackagedBackendArtifactResolver
import com.ghostlock.app.data.run.RootManagerArtifactResolver
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
import com.ghostlock.app.domain.payload.ReadPayloadEditorUseCase
import com.ghostlock.app.domain.payload.SavePayloadEditorUseCase
import com.ghostlock.app.domain.run.PreviewRootScriptUseCase
import com.ghostlock.app.domain.run.RunLogDocumentFormatter
import com.ghostlock.app.core.rootscript.PreviewRootScriptPublisher
import com.ghostlock.app.core.rootscript.RootScriptRenderer
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
    private val backendRegistryEntries = BackendRegistry.specs.toList()
    private val backendSessionStore = BackendSessionStore()
    private val backendConfigurationAdapter = AndroidBackendConfigurationAdapter(
        registryEntries = backendRegistryEntries,
        availability = BackendRegistry::availability,
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
        selectedKey = { backendSessionStore.selectedKey },
        availability = BackendRegistry::availability,
    )
    private val getBackendCatalog = GetBackendCatalogUseCase(backendCatalogAdapter)
    private val selectBackend = SelectBackendUseCase(
        BackendSelectionAdapter(backendRegistryEntries, backendSessionStore),
    )
    private val payloadRepository = PayloadDocumentRepository(context)
    private val finishRepository = FinishDocumentRepository(context)
    private val packagedBackendArtifactResolver = PackagedBackendArtifactResolver(context) { backendKey ->
        BackendRegistry.plugin(backendKey)?.carrierArtifact
    }
    private val rootManagerArtifactResolver = RootManagerArtifactResolver(context)
    private val runInputFreezer = RunInputFreezer(
        context = context,
        payloadRepository = payloadRepository,
        finishRepository = finishRepository,
        packagedCarrierResolver = packagedBackendArtifactResolver,
        rootManagerResolver = rootManagerArtifactResolver,
    )
    private val runLogDocumentFormatter = RunLogDocumentFormatter()
    private val runCoordinator = RunCoordinator(
        sinkFactory = AndroidRunLogSinkFactory(applicationContext),
        formatter = runLogDocumentFormatter,
    )
    private val runBackend = RunBackendUseCase(
        BackendRunSessionAdapter(
            context = context,
            registryEntries = backendRegistryEntries,
            availability = BackendRegistry::availability,
            selectedKey = { backendSessionStore.selectedKey },
            coordinator = runCoordinator,
            freezer = runInputFreezer,
            appClient = AppRunHostClient(context),
            shizukuClient = ShizukuRunHostClient(context),
            observeOperationalPreferences = observeOperationalPreferences,
            observePrivilege = observePrivilege,
        ),
    )
    private val diagnosticReportAdapter = AndroidDiagnosticReportBuilder(
        context = context,
        runState = { runCoordinator.state.value },
        runLog = runCoordinator::renderCanonicalDocument,
    )
    private val readFinishSettings = ReadFinishSettingsUseCase(finishRepository)
    private val saveFinishSettings = SaveFinishSettingsUseCase(finishRepository)
    private val readPayloadEditor = ReadPayloadEditorUseCase(payloadRepository)
    private val savePayloadEditor = SavePayloadEditorUseCase(payloadRepository)
    private val previewRootScript = PreviewRootScriptUseCase(
        payloadRepository = payloadRepository,
        finishRepository = finishRepository,
        previewPort = PreviewRootScriptPublisher(
            previewRoot = java.io.File(applicationContext.filesDir, "root-script-preview"),
            renderer = RootScriptRenderer(),
        ),
    )

    /** Application diagnostic report command used by presentation save/share actions. */
    val createDiagnosticReport = CreateDiagnosticReportUseCase(diagnosticReportAdapter)

    /** Restores the captured backend selection and configuration before the first composition. */
    fun restoreBackendSession() {
        backendSessionStore.restore(backendRegistryEntries)
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
        readPayloadEditor = readPayloadEditor,
        savePayloadEditor = savePayloadEditor,
        previewRootScript = previewRootScript,
        runBackend = runBackend,
        appVersionName = BuildConfig.VERSION_NAME,
        appVersionCode = BuildConfig.VERSION_CODE.toLong(),
        isPrBuild = BuildConfig.IS_PR_BUILD,
    )
}
