package com.ghostlock.app.ui.viewmodel

import androidx.compose.runtime.Immutable
import com.ghostlock.app.ui.UiMode
import com.ghostlock.app.ui.theme.AppSettings

@Immutable
data class MainActivityUiState(
    val appSettings: AppSettings,
    val pageScale: Float,
    val enableBlur: Boolean,
    val enableFloatingBottomBar: Boolean,
    val enableFloatingBottomBarBlur: Boolean,
    val enableNavigationBadge: Boolean,
    val navigationRailExpanded: Boolean,
    val enableSwipeDismiss: Boolean,
    val pagerInterceptionMode: Int,
    val uiMode: UiMode,
)
