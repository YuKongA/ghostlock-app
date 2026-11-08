package com.ghostlock.app

import android.app.Application
import android.content.Context
import androidx.lifecycle.ViewModelProvider
import com.ghostlock.app.data.runhost.client.AppRunHostClient
import com.ghostlock.app.data.runhost.client.ShizukuRunHostClient
import com.ghostlock.app.composition.backend.BackendRegistry
import com.ghostlock.app.core.run.LogHub
import com.ghostlock.app.core.run.RunInputFreezer
import com.ghostlock.app.data.run.AndroidRunSessionAdapter
import com.ghostlock.app.data.backend.BackendSelectionAdapter
import com.ghostlock.app.data.backend.BackendSessionStore
import com.ghostlock.app.data.device.AndroidDeviceStatusAdapter
import com.ghostlock.app.data.diagnostics.AndroidDiagnosticReportBuilder
import com.ghostlock.app.data.finish.DataStoreFinishRepository
import com.ghostlock.app.data.payload.DataStorePayloadRepository
import com.ghostlock.app.data.privilege.AndroidPrivilegeAdapter
import com.ghostlock.app.data.run.RootManagerArtifactResolver
import com.ghostlock.app.data.settings.DataStoreAppSelectionRepository
import com.ghostlock.app.data.settings.DataStoreOperationalPreferencesRepository
import com.ghostlock.app.data.settings.DataStoreUiPreferencesRepository
import com.ghostlock.app.data.update.DisabledAppUpdateAdapter
import com.ghostlock.app.domain.backend.SelectBackendUseCase
import com.ghostlock.app.domain.run.RunBackendUseCase
import com.ghostlock.app.domain.device.ReadDeviceStatusUseCase
import com.ghostlock.app.domain.diagnostics.CreateDiagnosticReportUseCase
import com.ghostlock.app.domain.finish.ReadFinishSettingsUseCase
import com.ghostlock.app.domain.finish.SaveFinishSettingsUseCase
import com.ghostlock.app.domain.payload.ReadPayloadEditorUseCase
import com.ghostlock.app.domain.payload.SavePayloadEditorUseCase
import com.ghostlock.app.domain.run.PreviewRootScriptUseCase
import com.ghostlock.app.domain.run.LogDocumentFormatter
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
import com.ghostlock.app.ui.backend.BackendUiCatalog
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.async

/**
 * Application-scoped composition root for Clean Architecture adapters and use cases.
 *
 * @param context Application context used to construct Android data adapters once.
 * @property viewModelFactory Factory that injects the settings and device-domain use cases.
 * @note Physical adapter lifetime equals the application process; presentation never constructs it directly.
 * The sole LogHub is source-neutral: run adapters attach already-instantiated connectors rather than factories.
 */
class AppContainer(context: Context) {
    private val applicationContext = context.applicationContext
    private val applicationScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    init {
        check(Application.getProcessName() == applicationContext.packageName) {
            "AppContainer is restricted to the main application process"
        }
    }
    private val backendDescriptors = BackendRegistry.descriptors.toList()
    private val appSelectionRepository = DataStoreAppSelectionRepository.create(
        applicationContext,
        applicationScope,
    )
    private val backendSessionStore = BackendSessionStore(appSelectionRepository)
    /** Sole UI-owned projection of the immutable built-in definition snapshot. */
    val backendUiCatalog = BackendUiCatalog(
        entries = BackendRegistry.uiEntries,
        selectedKey = { backendSessionStore.selectedKey },
        availability = BackendRegistry::availability,
        text = applicationContext::getString,
    )
    private val uiPreferencesRepository = DataStoreUiPreferencesRepository.create(
        applicationContext,
        applicationScope,
    )
    private val operationalPreferencesAdapter = DataStoreOperationalPreferencesRepository.create(
        applicationContext,
        applicationScope,
    )
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
    private val privilegeAdapter = AndroidPrivilegeAdapter(applicationContext, appSelectionRepository)
    private val observePrivilege = ObservePrivilegeUseCase(privilegeAdapter)
    private val refreshPrivilege = RefreshPrivilegeUseCase(privilegeAdapter)
    private val selectPrivilegeExecutor = SelectPrivilegeExecutorUseCase(privilegeAdapter)
    private val deviceStatusAdapter = AndroidDeviceStatusAdapter()
    private val readDeviceStatus = ReadDeviceStatusUseCase(deviceStatusAdapter)
    private val checkAppUpdate = CheckAppUpdateUseCase(DisabledAppUpdateAdapter())
    private val selectBackend = SelectBackendUseCase(
        BackendSelectionAdapter(backendDescriptors, backendSessionStore),
    )
    private val payloadRepository = DataStorePayloadRepository.create(applicationContext, applicationScope)
    private val finishRepository = DataStoreFinishRepository.create(applicationContext, applicationScope)
    private val rootManagerArtifactResolver = RootManagerArtifactResolver(context)
    private val runInputFreezer = RunInputFreezer(
        context = context,
        payloadRepository = payloadRepository,
        finishRepository = finishRepository,
        rootManagerResolver = rootManagerArtifactResolver,
    )
    private val logDocumentFormatter = LogDocumentFormatter()
    private val logHub = LogHub(logDocumentFormatter)
    private val runBackend = RunBackendUseCase(
        AndroidRunSessionAdapter(
            context = context,
            registryEntries = backendDescriptors,
            availability = BackendRegistry::availability,
            backendLabel = { key ->
                backendUiCatalog.entry(key)?.let(backendUiCatalog::displayName)
            },
            selectedKey = { backendSessionStore.selectedKey },
            logHub = logHub,
            formatter = logDocumentFormatter,
            freezer = runInputFreezer,
            appClient = AppRunHostClient(context),
            shizukuClient = ShizukuRunHostClient(context),
            operationalPreferencesPort = operationalPreferencesAdapter,
            applicationScope = applicationScope,
            observePrivilege = observePrivilege,
        ),
    )
    private val diagnosticReportAdapter = AndroidDiagnosticReportBuilder(
        context = context,
        runState = { logHub.state.value },
        runLog = logHub::filePrefix,
        formatter = logDocumentFormatter,
    )
    private val readFinishSettings = ReadFinishSettingsUseCase(finishRepository)
    private val saveFinishSettings = SaveFinishSettingsUseCase(finishRepository)
    private val readPayloadEditor = ReadPayloadEditorUseCase(payloadRepository)
    private val savePayloadEditor = SavePayloadEditorUseCase(payloadRepository)
    private val previewRootScript = PreviewRootScriptUseCase(
        previewPort = PreviewRootScriptPublisher(
            previewRoot = java.io.File(applicationContext.filesDir, "root-script-preview"),
            renderer = RootScriptRenderer(),
        ),
    )

    /** Application diagnostic report command used by presentation save/share actions. */
    val createDiagnosticReport = CreateDiagnosticReportUseCase(diagnosticReportAdapter)

    private val initialization = applicationScope.async(start = CoroutineStart.LAZY) {
        uiPreferencesRepository.awaitInitial()
        operationalPreferencesAdapter.awaitInitial()
        appSelectionRepository.awaitInitial()
        backendSessionStore.restore(backendDescriptors)
        privilegeAdapter.refresh()
    }

    /** Waits the sole application-scoped initialization task without duplicating it across Activities. */
    suspend fun initialize() = initialization.await()

    val viewModelFactory: ViewModelProvider.Factory = AppViewModelFactory(
        observeUiPreferences = observeUiPreferences,
        observeOperationalPreferences = observeOperationalPreferences,
        saveUiPreferences = saveUiPreferences,
        saveOperationalPreferences = saveOperationalPreferences,
        importTerminalFont = importTerminalFont,
        selectLogExportDirectory = selectLogExportDirectory,
        setNavigationRailExpanded = setNavigationRailExpanded,
        readDeviceStatus = readDeviceStatus,
        backendUiCatalog = backendUiCatalog,
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
