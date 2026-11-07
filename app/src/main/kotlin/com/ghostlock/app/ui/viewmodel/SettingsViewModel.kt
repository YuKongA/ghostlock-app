package com.ghostlock.app.ui.viewmodel

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.ghostlock.app.domain.settings.ObserveUiPreferencesUseCase
import com.ghostlock.app.domain.settings.ImportTerminalFontUseCase
import com.ghostlock.app.domain.settings.ObserveOperationalPreferencesUseCase
import com.ghostlock.app.domain.settings.OperationalPreferences
import com.ghostlock.app.domain.settings.SaveUiPreferencesUseCase
import com.ghostlock.app.domain.settings.SaveOperationalPreferencesUseCase
import com.ghostlock.app.domain.settings.SelectLogExportDirectoryUseCase
import com.ghostlock.app.domain.settings.TerminalLogLevel
import com.ghostlock.app.domain.settings.UiPreferences
import com.ghostlock.app.ui.screen.settings.SettingsUiState
import com.ghostlock.app.ui.theme.ColorMode
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.stateIn

/**
 * Owns editable application-shell settings through storage-neutral domain use cases.
 *
 * @param observeUiPreferences Live preference snapshot source.
 * @param saveUiPreferences Command boundary for committing an updated snapshot.
 * @note Removed manager feature toggles are intentionally absent; this model only owns rendered GhostLock settings.
 */
class SettingsViewModel(
    observeUiPreferences: ObserveUiPreferencesUseCase,
    private val saveUiPreferences: SaveUiPreferencesUseCase,
    observeOperationalPreferences: ObserveOperationalPreferencesUseCase,
    private val saveOperationalPreferences: SaveOperationalPreferencesUseCase,
    private val importTerminalFont: ImportTerminalFontUseCase,
    private val selectLogExportDirectoryUseCase: SelectLogExportDirectoryUseCase,
) : ViewModel() {
    private val preferences = observeUiPreferences()
    private val operationalPreferences = observeOperationalPreferences()

    val uiState: StateFlow<SettingsUiState> = combine(
        preferences,
        operationalPreferences,
        ::toUiState,
    )
        .stateIn(
            scope = viewModelScope,
            started = SharingStarted.Eagerly,
            initialValue = toUiState(preferences.value, operationalPreferences.value),
        )

    /** Persists whether the home screen may check for an application update. */
    fun setCheckUpdate(enabled: Boolean) = update { copy(checkUpdate = enabled) }

    /**
     * Changes the renderer family while preserving the historical Monet conversion behavior.
     *
     * @param mode Persisted `material` or `miuix` token.
     */
    fun setUiMode(mode: String) {
        val current = preferences.value
        val colorMode = ColorMode.fromValue(current.themeMode)
        val nextThemeMode = when (current.uiMode) {
            "material" -> if (mode == "miuix") {
                val baseMode = if (colorMode == ColorMode.DARK_AMOLED) 2 else current.themeMode
                when {
                    current.miuixMonet && !colorMode.isMonet -> ColorMode.fromValue(baseMode).toMonetMode()
                    !current.miuixMonet && colorMode.isMonet -> ColorMode.fromValue(baseMode).toNonMonetMode()
                    else -> baseMode
                }
            } else current.themeMode

            "miuix" -> if (mode == "material" && colorMode.isMonet) {
                colorMode.toNonMonetMode()
            } else current.themeMode

            else -> current.themeMode
        }
        saveUiPreferences(current.copy(uiMode = mode, themeMode = nextThemeMode))
    }

    /** Persists the base theme-mode ordinal. */
    fun setThemeMode(mode: Int) {
        val current = preferences.value
        val effective = if (current.uiMode == "miuix" && current.miuixMonet) mode + 3 else mode
        saveUiPreferences(current.copy(themeMode = effective))
    }

    /** Persists an already-normalized color mode. */
    fun setColorMode(mode: ColorMode) = update { copy(themeMode = mode.value) }

    /** Enables or disables the Miuix Monet family while preserving the base light/dark choice. */
    fun setMiuixMonet(enabled: Boolean) {
        val current = preferences.value
        val colorMode = ColorMode.fromValue(current.themeMode)
        val nextThemeMode = when {
            enabled && !colorMode.isMonet -> colorMode.toMonetMode()
            !enabled && colorMode.isMonet -> colorMode.toNonMonetMode()
            else -> current.themeMode
        }
        saveUiPreferences(current.copy(miuixMonet = enabled, themeMode = nextThemeMode))
    }

    /** Persists the selected key color. */
    fun setKeyColor(color: Int) = update { copy(keyColor = color) }

    /** Persists the Material palette-style token. */
    fun setColorStyle(style: String) = update { copy(colorStyle = style) }

    /** Persists the Material color-spec token. */
    fun setColorSpec(spec: String) = update { copy(colorSpec = spec) }

    /** Persists Android predictive-back opt-in. */
    fun setEnablePredictiveBack(enabled: Boolean) = update { copy(enablePredictiveBack = enabled) }

    /** Persists page swipe-dismiss behavior. */
    fun setEnableSwipeDismiss(enabled: Boolean) = update { copy(enableSwipeDismiss = enabled) }

    /** Persists the bounded pager interception mode. */
    fun setPagerInterceptionMode(mode: Int) = update {
        copy(pagerInterceptionMode = mode.coerceIn(0, 2))
    }

    /** Persists Miuix background blur. */
    fun setEnableBlur(enabled: Boolean) = update { copy(enableBlur = enabled) }

    /** Persists floating bottom chrome. */
    fun setEnableFloatingBottomBar(enabled: Boolean) = update { copy(enableFloatingBottomBar = enabled) }

    /** Persists floating bottom chrome blur. */
    fun setEnableFloatingBottomBarBlur(enabled: Boolean) = update {
        copy(enableFloatingBottomBarBlur = enabled)
    }

    /** Persists navigation badge visibility. */
    fun setEnableNavigationBadge(enabled: Boolean) = update { copy(enableNavigationBadge = enabled) }

    /** Persists the page-scale multiplier. */
    fun setPageScale(scale: Float) = update { copy(pageScale = scale) }

    /** Persists the selected log threshold by its renderer-neutral list index. */
    fun setLogLevelIndex(index: Int) = updateOperational {
        copy(logLevel = TerminalLogLevel.entries.getOrElse(index) { logLevel })
    }

    /**
     * Imports a document-picker font through the domain effect boundary.
     *
     * @param uri Serialized Android document URI.
     */
    fun selectTerminalFont(uri: String) {
        importTerminalFont(uri)
    }

    /** Persists terminal timestamp visibility. */
    fun setShowLogTimestamp(enabled: Boolean) = updateOperational {
        copy(showLogTimestamp = enabled)
    }

    /** Persists whether each accepted run creates an automatic GLLOG2 document. */
    fun setAutoExportLogs(enabled: Boolean) = updateOperational {
        copy(autoExportLogs = enabled)
    }

    /**
     * Selects a Downloads-compatible automatic-export directory.
     *
     * @param uri Serialized Android tree URI.
     * @return True when the adapter normalized and persisted the tree.
     */
    fun selectLogExportDirectory(uri: String): Boolean = selectLogExportDirectoryUseCase(uri)

    /** Persists the rerun confirmation preference consumed by the run domain adapter. */
    fun setWarnOnRerun(enabled: Boolean) = updateOperational {
        copy(warnOnRerun = enabled)
    }

    /** Persists the dormant boot-run preference without starting any receiver or service. */
    fun setAutoAttackOnBoot(enabled: Boolean) = updateOperational {
        copy(autoAttackOnBoot = enabled)
    }

    /** Persists a bounded boot retry count. */
    fun setBootRetryCount(count: Int) = updateOperational {
        copy(bootRetryCount = count.coerceIn(0, 10))
    }

    /** Applies one atomic copy operation to the current domain snapshot. */
    private fun update(transform: UiPreferences.() -> UiPreferences) {
        saveUiPreferences(preferences.value.transform())
    }

    /** Applies one atomic copy operation to the current operational domain snapshot. */
    private fun updateOperational(
        transform: OperationalPreferences.() -> OperationalPreferences,
    ) {
        saveOperationalPreferences(operationalPreferences.value.transform())
    }

    /** Maps the domain snapshot into the renderer contract without storage access. */
    private fun toUiState(
        value: UiPreferences,
        operational: OperationalPreferences,
    ): SettingsUiState = SettingsUiState(
        uiMode = value.uiMode,
        checkUpdate = value.checkUpdate,
        themeMode = value.themeMode,
        miuixMonet = value.miuixMonet,
        keyColor = value.keyColor,
        colorStyle = value.colorStyle,
        colorSpec = value.colorSpec,
        enablePredictiveBack = value.enablePredictiveBack,
        enableSwipeDismiss = value.enableSwipeDismiss,
        pagerInterceptionMode = value.pagerInterceptionMode,
        enableBlur = value.enableBlur,
        enableFloatingBottomBar = value.enableFloatingBottomBar,
        enableFloatingBottomBarBlur = value.enableFloatingBottomBarBlur,
        enableNavigationBadge = value.enableNavigationBadge,
        pageScale = value.pageScale,
        logLevelOptions = TerminalLogLevel.entries.map(TerminalLogLevel::token),
        selectedLogLevelIndex = TerminalLogLevel.entries.indexOf(operational.logLevel)
            .coerceAtLeast(0),
        terminalFontName = operational.terminalFontPath
            ?.substringAfterLast('/')
            ?.substringAfterLast('\\'),
        showLogTimestamp = operational.showLogTimestamp,
        autoExportLogs = operational.autoExportLogs,
        logExportLocation = operational.logExportLocation,
        warnOnRerun = operational.warnOnRerun,
        autoAttackOnBoot = operational.autoAttackOnBoot,
        bootRetryCount = operational.bootRetryCount,
    )
}
