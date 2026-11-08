package com.ghostlock.app.ui.viewmodel

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.ghostlock.app.domain.backend.SelectBackendUseCase
import com.ghostlock.app.ui.backend.BackendUiCatalog
import com.ghostlock.app.ui.screen.module.BackendOptionUiState
import com.ghostlock.app.ui.screen.module.BackendPageUiState
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

/**
 * Owns renderer-neutral path-page selection state through injected domain use cases.
 *
 * @param backendUiCatalog Reads the captured UI projection and current stable selection.
 * @param selectBackend Persists a requested stable key through the data adapter.
 * @note Backend execution remains outside this ViewModel; the path page only selects and configures.
 */
class PathViewModel(
    private val backendUiCatalog: BackendUiCatalog,
    private val selectBackend: SelectBackendUseCase,
) : ViewModel() {
    private val mutableUiState = MutableStateFlow(buildState())
    val uiState: StateFlow<BackendPageUiState> = mutableUiState.asStateFlow()

    /** Refreshes selection and localized labels from the injected catalog projection. */
    fun refresh() {
        mutableUiState.value = buildState()
    }

    /**
     * Persists one selectable backend and refreshes the shared page model on success.
     *
     * @param key Stable key from [BackendPageUiState.options].
     */
    fun selectBackend(key: String) {
        viewModelScope.launch {
            if (selectBackend.invoke(key)) refresh()
        }
    }

    /** @return Current renderer contract without Android, registry, or storage dependencies. */
    private fun buildState(): BackendPageUiState {
        val selected = backendUiCatalog.selectedEntry()
        return BackendPageUiState(
            options = backendUiCatalog.entries.map { entry ->
                BackendOptionUiState(
                    key = entry.descriptor.key,
                    title = backendUiCatalog.displayName(entry),
                    selectable = entry.descriptor.selectable,
                    icon = entry.ui.icon,
                )
            },
            selectedKey = selected?.descriptor?.key.orEmpty(),
        )
    }
}
