package com.ghostlock.app.ui.viewmodel

import androidx.compose.runtime.Composable
import com.ghostlock.app.domain.backend.AbortPolicy
import com.ghostlock.app.domain.backend.BackendAvailability
import com.ghostlock.app.domain.backend.BackendDescriptor
import com.ghostlock.app.domain.backend.BackendSelectionPolicy
import com.ghostlock.app.domain.backend.ExecutionDomain
import com.ghostlock.app.domain.device.DeviceStatus
import com.ghostlock.app.domain.device.DeviceStatusPort
import com.ghostlock.app.domain.device.KernelVersion
import com.ghostlock.app.domain.device.ReadDeviceStatusUseCase
import com.ghostlock.app.domain.privilege.AttackExecutorKind
import com.ghostlock.app.domain.privilege.ExecutorSelectionResult
import com.ghostlock.app.domain.privilege.ObservePrivilegeUseCase
import com.ghostlock.app.domain.privilege.PrivilegePort
import com.ghostlock.app.domain.privilege.PrivilegeState
import com.ghostlock.app.domain.privilege.RefreshPrivilegeUseCase
import com.ghostlock.app.domain.privilege.SelectPrivilegeExecutorUseCase
import com.ghostlock.app.domain.run.RunCapability
import com.ghostlock.app.domain.settings.ObserveUiPreferencesUseCase
import com.ghostlock.app.domain.settings.UiPreferences
import com.ghostlock.app.domain.settings.UiPreferencesPort
import com.ghostlock.app.domain.update.AppUpdateInfo
import com.ghostlock.app.domain.update.AppUpdatePort
import com.ghostlock.app.domain.update.CheckAppUpdateUseCase
import com.ghostlock.app.ui.backend.BackendUi
import com.ghostlock.app.ui.backend.BackendUiCatalog
import com.ghostlock.app.ui.backend.BackendUiEntry
import com.ghostlock.app.ui.icon.IconPair
import com.ghostlock.app.ui.icon.Icons
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.Deferred
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.resetMain
import kotlinx.coroutines.test.setMain
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test

/** Locks the home status card to exact catalog selection and terminal catalog failure. */
@OptIn(ExperimentalCoroutinesApi::class)
class HomeViewModelTest {
    private val dispatcher = StandardTestDispatcher()

    @Before
    fun setMainDispatcher() {
        Dispatchers.setMain(dispatcher)
    }

    @After
    fun resetMainDispatcher() {
        Dispatchers.resetMain()
    }

    /** Exact selectable membership is the only state projected as ready. */
    @Test
    fun exactSelectionProjectsReadyBackendIdentity() {
        val state = viewModel(
            catalog(entries = { listOf(entry()) }, selectedKey = { "DIAGNOSE" }),
        ).uiState.value.selectedBackend

        assertEquals("category", state.cveLabel)
        assertEquals("Ready", state.statusText)
        assertTrue(state.isReady)
    }

    /** READY without a persisted selection stays not-ready and does not borrow the first identity. */
    @Test
    fun noSelectionDoesNotManufactureBackendIdentity() {
        val state = viewModel(
            catalog(entries = { listOf(entry()) }, selectedKey = { null }),
        ).uiState.value.selectedBackend

        assertEquals("", state.cveLabel)
        assertEquals("no-backend", state.statusText)
        assertFalse(state.isReady)
    }

    /** FAILED exposes the bounded reason outside backend membership. */
    @Test
    fun catalogFailureProjectsReasonWithoutBackendIdentity() {
        val state = viewModel(
            catalog(
                entries = { emptyList() },
                selectedKey = { null },
                failureReason = { "content root mismatch" },
            ),
        ).uiState.value.selectedBackend

        assertEquals("", state.cveLabel)
        assertEquals("catalog-failed: content root mismatch", state.statusText)
        assertFalse(state.isReady)
    }

    private fun viewModel(catalog: BackendUiCatalog): HomeViewModel {
        val preferences = TestUiPreferencesPort()
        val privilege = TestPrivilegePort()
        return HomeViewModel(
            readDeviceStatus = ReadDeviceStatusUseCase(
                object : DeviceStatusPort {
                    override fun read(): DeviceStatus = DeviceStatus(
                        kernelVersion = KernelVersion(6, 1, 0),
                        kernelRelease = "6.1.0-test",
                        selinuxStatus = "Enforcing",
                        deviceModel = "test-device",
                        fingerprint = "test/fingerprint",
                        seccompStatus = 2,
                    )
                },
            ),
            backendUiCatalog = catalog,
            observeUiPreferences = ObserveUiPreferencesUseCase(preferences),
            checkAppUpdate = CheckAppUpdateUseCase(
                object : AppUpdatePort {
                    override suspend fun check(): AppUpdateInfo = AppUpdateInfo()
                },
            ),
            observePrivilege = ObservePrivilegeUseCase(privilege),
            refreshPrivilege = RefreshPrivilegeUseCase(privilege),
            selectPrivilegeExecutor = SelectPrivilegeExecutorUseCase(privilege),
            appVersionName = "test",
            appVersionCode = 1,
            isPrBuild = false,
        )
    }

    private fun catalog(
        entries: () -> List<BackendUiEntry>,
        selectedKey: () -> String?,
        failureReason: () -> String? = { null },
    ): BackendUiCatalog = BackendUiCatalog(
        entries = entries,
        selectedKey = selectedKey,
        catalogFailureReason = failureReason,
        availability = { BackendAvailability.Ready() },
        text = { resource -> if (resource == 1) "category" else "no-backend" },
        catalogFailureText = { reason -> "catalog-failed: $reason" },
    )

    private fun entry(): BackendUiEntry = BackendUiEntry(
        descriptor = BackendDescriptor(
            key = "DIAGNOSE",
            selectionPolicy = BackendSelectionPolicy.SELECTABLE,
            abortPolicy = AbortPolicy.BEFORE_START_ONLY,
            executionDomain = ExecutionDomain.APP,
            runCapability = RunCapability.DIAGNOSTIC_ONLY,
        ),
        ui = TestBackendUi,
    )

    private object TestBackendUi : BackendUi {
        override val categoryLabelRes: Int = 1
        override val displayNameRes: Int = 2
        override val icon: IconPair = Icons.Filled.BugReport

        /** Test-only content; this suite exercises the renderer-neutral home projection. */
        @Composable
        override fun Content(availability: BackendAvailability) = Unit
    }

    private class TestUiPreferencesPort : UiPreferencesPort {
        override val preferences = MutableStateFlow(UiPreferences(checkUpdate = false))

        override fun submit(transform: (UiPreferences) -> UiPreferences): Deferred<UiPreferences> =
            CompletableDeferred(transform(preferences.value).also { preferences.value = it })

        override suspend fun awaitInitial(): UiPreferences = preferences.value

        override fun snapshot(): Deferred<UiPreferences> = CompletableDeferred(preferences.value)
    }

    private class TestPrivilegePort : PrivilegePort {
        override val state = MutableStateFlow(
            PrivilegeState(
                selectedExecutor = AttackExecutorKind.APP,
                appUid = 12345,
                shizukuAvailable = false,
                shizukuGranted = false,
            ),
        )

        override fun refresh() = Unit

        override suspend fun select(kind: AttackExecutorKind): ExecutorSelectionResult =
            ExecutorSelectionResult.SELECTED
    }
}
