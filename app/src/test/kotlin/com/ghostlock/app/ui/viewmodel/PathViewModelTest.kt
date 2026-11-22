package com.ghostlock.app.ui.viewmodel

import androidx.compose.runtime.Composable
import com.ghostlock.app.domain.backend.AbortPolicy
import com.ghostlock.app.domain.backend.BackendAvailability
import com.ghostlock.app.domain.backend.BackendDescriptor
import com.ghostlock.app.domain.backend.BackendSelectionPolicy
import com.ghostlock.app.domain.backend.BackendSelectionPort
import com.ghostlock.app.domain.backend.ExecutionDomain
import com.ghostlock.app.domain.backend.SelectBackendUseCase
import com.ghostlock.app.domain.run.RunCapability
import com.ghostlock.app.ui.backend.BackendUi
import com.ghostlock.app.ui.backend.BackendUiCatalog
import com.ghostlock.app.ui.backend.BackendUiEntry
import com.ghostlock.app.ui.icon.IconPair
import com.ghostlock.app.ui.icon.Icons
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.advanceUntilIdle
import kotlinx.coroutines.test.resetMain
import kotlinx.coroutines.test.runTest
import kotlinx.coroutines.test.setMain
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Before
import org.junit.Test

/** Locks the path selector to catalog membership and explicit persisted selection. */
@OptIn(ExperimentalCoroutinesApi::class)
class PathViewModelTest {
    private val dispatcher = StandardTestDispatcher()

    @Before
    fun setMainDispatcher() {
        Dispatchers.setMain(dispatcher)
    }

    @After
    fun resetMainDispatcher() {
        Dispatchers.resetMain()
    }

    /** READY membership without a choice keeps both key and index null until selection commits. */
    @Test
    fun readyCatalogRequiresExplicitSuccessfulSelection() = runTest(dispatcher) {
        var selectedKey: String? = null
        val entry = entry("DIAGNOSE")
        val catalog = catalog(
            entries = { listOf(entry) },
            selectedKey = { selectedKey },
        )
        val viewModel = PathViewModel(
            backendUiCatalog = catalog,
            selectBackend = SelectBackendUseCase(
                object : BackendSelectionPort {
                    override suspend fun select(key: String): Boolean {
                        if (key != entry.descriptor.key) return false
                        selectedKey = key
                        return true
                    }
                },
            ),
        )

        assertEquals(listOf("DIAGNOSE"), viewModel.uiState.value.options.map { it.key })
        assertNull(viewModel.uiState.value.selectedKey)
        assertNull(viewModel.uiState.value.selectedIndex)

        viewModel.selectBackend("missing")
        advanceUntilIdle()
        assertNull(viewModel.uiState.value.selectedKey)

        viewModel.selectBackend("DIAGNOSE")
        advanceUntilIdle()
        assertEquals("DIAGNOSE", viewModel.uiState.value.selectedKey)
        assertEquals(0, viewModel.uiState.value.selectedIndex)
    }

    /** FAILED is projected outside membership and never becomes a synthetic selector row. */
    @Test
    fun failedCatalogExposesReasonWithNoOptions() {
        val viewModel = PathViewModel(
            backendUiCatalog = catalog(
                entries = { emptyList() },
                selectedKey = { null },
                failureReason = { "builtin archive rejected" },
            ),
            selectBackend = SelectBackendUseCase(
                object : BackendSelectionPort {
                    override suspend fun select(key: String): Boolean = false
                },
            ),
        )

        assertEquals(emptyList<String>(), viewModel.uiState.value.options.map { it.key })
        assertNull(viewModel.uiState.value.selectedKey)
        assertNull(viewModel.uiState.value.selectedIndex)
        assertEquals("builtin archive rejected", viewModel.uiState.value.catalogFailureReason)
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
        text = { resource -> "text-$resource" },
        catalogFailureText = { reason -> "catalog-failed: $reason" },
    )

    private fun entry(key: String): BackendUiEntry = BackendUiEntry(
        descriptor = BackendDescriptor(
            key = key,
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

        /** Test-only content; this suite exercises renderer-neutral selection state. */
        @Composable
        override fun Content(availability: BackendAvailability) = Unit
    }
}
