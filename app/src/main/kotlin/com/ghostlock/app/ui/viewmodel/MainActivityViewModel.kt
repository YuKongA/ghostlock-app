package com.ghostlock.app.ui.viewmodel

import androidx.lifecycle.SavedStateHandle
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.ghostlock.app.domain.settings.ObserveUiPreferencesUseCase
import com.ghostlock.app.domain.settings.SetNavigationRailExpandedUseCase
import com.ghostlock.app.domain.settings.UiPreferences
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn
import com.ghostlock.app.ui.UiMode
import com.ghostlock.app.ui.theme.ThemeController

/**
 * Owns application-shell state while depending only on the settings domain use case.
 *
 * @param savedStateHandle Activity navigation-state owner.
 * @param observeUiPreferences Domain stream for renderer preferences.
 * @param setNavigationRailExpanded Domain command for the only shell preference mutated by navigation chrome.
 * @note Typed-store ownership belongs to the data adapter in AppContainer.
 */
class MainActivityViewModel(
    savedStateHandle: SavedStateHandle,
    observeUiPreferences: ObserveUiPreferencesUseCase,
    private val setNavigationRailExpanded: SetNavigationRailExpandedUseCase,
) : ViewModel() {
    private val mainPageState = MainPageState(savedStateHandle)
    private val preferences = observeUiPreferences()

    val uiState: StateFlow<MainActivityUiState> = preferences
        .map(::toUiState)
        .stateIn(
            scope = viewModelScope,
            started = SharingStarted.Eagerly,
            initialValue = toUiState(preferences.value),
        )
    val selectedMainPage: StateFlow<Int> = mainPageState.selectedPage

    /** Updates only the Activity-owned pager position. */
    fun setSelectedMainPage(page: Int) {
        mainPageState.updateSelectedPage(page)
    }

    /** Persists the wide-screen rail state through the domain command boundary. */
    fun updateNavigationRailExpanded(expanded: Boolean) {
        setNavigationRailExpanded(expanded)
    }

    /**
     * Maps one domain snapshot to the existing presentation contract.
     *
     * @param value Storage-neutral shell preferences.
     * @return Current MainActivity renderer state.
     */
    private fun toUiState(value: UiPreferences): MainActivityUiState {
        return MainActivityUiState(
            appSettings = ThemeController.getAppSettings(value),
            pageScale = value.pageScale,
            enableBlur = value.enableBlur,
            enableFloatingBottomBar = value.enableFloatingBottomBar,
            enableFloatingBottomBarBlur = value.enableFloatingBottomBarBlur,
            enableNavigationBadge = value.enableNavigationBadge,
            navigationRailExpanded = value.navigationRailExpanded,
            enableSwipeDismiss = value.enableSwipeDismiss,
            pagerInterceptionMode = value.pagerInterceptionMode,
            uiMode = UiMode.fromValue(value.uiMode),
        )
    }
}

private const val SELECTED_MAIN_PAGE_KEY = "selected_main_page"

private class MainPageState(
    private val savedStateHandle: SavedStateHandle,
) {
    val selectedPage: StateFlow<Int> = savedStateHandle.getStateFlow(SELECTED_MAIN_PAGE_KEY, 0)

    fun updateSelectedPage(page: Int) {
        savedStateHandle[SELECTED_MAIN_PAGE_KEY] = MainPagerConfig.coercePage(page)
    }
}

object MainPagerConfig {
    const val PAGE_COUNT = 4
    const val LAST_PAGE_INDEX = PAGE_COUNT - 1

    fun coercePage(page: Int): Int = page.coerceIn(0, LAST_PAGE_INDEX)
}
