package com.ghostlock.app.data.settings

import android.content.Context
import android.content.SharedPreferences
import com.ghostlock.app.domain.settings.UiPreferences
import com.ghostlock.app.domain.settings.UiPreferencesPort
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/**
 * Android SharedPreferences adapter for the application-shell preference domain port.
 *
 * @param context Application-capable context used only to acquire the existing `settings` store.
 * @note The adapter preserves every existing key/default and is owned for the application process lifetime.
 */
class SharedPreferencesUiPreferencesRepository(
    context: Context,
) : UiPreferencesPort {
    private val preferencesStore = context.applicationContext.getSharedPreferences(
        PREFERENCES_NAME,
        Context.MODE_PRIVATE,
    )
    private val mutablePreferences = MutableStateFlow(readPreferences())
    private val listener = SharedPreferences.OnSharedPreferenceChangeListener { _, key ->
        if (key == null || key in OBSERVED_KEYS) {
            mutablePreferences.value = readPreferences()
        }
    }

    override val preferences: StateFlow<UiPreferences> = mutablePreferences.asStateFlow()

    init {
        preferencesStore.registerOnSharedPreferenceChangeListener(listener)
    }

    /**
     * Writes one immutable domain snapshot to the historical preference keys.
     *
     * @param value Complete application-shell preference state.
     * @note The in-memory stream is advanced immediately; the registered listener reconciles external writes.
     */
    override fun save(value: UiPreferences) {
        if (mutablePreferences.value == value) return
        preferencesStore.edit()
            .putString(KEY_UI_MODE, value.uiMode)
            .putBoolean(KEY_CHECK_UPDATE, value.checkUpdate)
            .putInt(KEY_THEME_MODE, value.themeMode)
            .putBoolean(KEY_MIUIX_MONET, value.miuixMonet)
            .putInt(KEY_KEY_COLOR, value.keyColor)
            .putString(KEY_COLOR_STYLE, value.colorStyle)
            .putString(KEY_COLOR_SPEC, value.colorSpec)
            .putFloat(KEY_PAGE_SCALE, value.pageScale)
            .putBoolean(KEY_ENABLE_BLUR, value.enableBlur)
            .putBoolean(KEY_ENABLE_FLOATING_BOTTOM_BAR, value.enableFloatingBottomBar)
            .putBoolean(KEY_ENABLE_FLOATING_BOTTOM_BAR_BLUR, value.enableFloatingBottomBarBlur)
            .putBoolean(KEY_ENABLE_NAVIGATION_BADGE, value.enableNavigationBadge)
            .putBoolean(KEY_NAVIGATION_RAIL_EXPANDED, value.navigationRailExpanded)
            .putBoolean(KEY_ENABLE_PREDICTIVE_BACK, value.enablePredictiveBack)
            .putBoolean(KEY_ENABLE_SWIPE_DISMISS, value.enableSwipeDismiss)
            .putInt(KEY_PAGER_INTERCEPTION_MODE, value.pagerInterceptionMode.coerceIn(0, 2))
            .apply()
        mutablePreferences.value = value.copy(
            pagerInterceptionMode = value.pagerInterceptionMode.coerceIn(0, 2),
        )
    }

    /**
     * Reads one immutable domain snapshot from the existing preference keys.
     *
     * @return Current application-shell preferences with the historical defaults.
     * @note Unknown enum-like strings remain strings here and are normalized by presentation mapping.
     */
    private fun readPreferences(): UiPreferences = UiPreferences(
        uiMode = preferencesStore.getString(KEY_UI_MODE, UiPreferences.DEFAULT_UI_MODE)
            ?: UiPreferences.DEFAULT_UI_MODE,
        checkUpdate = preferencesStore.getBoolean(KEY_CHECK_UPDATE, true),
        themeMode = preferencesStore.getInt(KEY_THEME_MODE, UiPreferences.DEFAULT_THEME_MODE),
        miuixMonet = preferencesStore.getBoolean(KEY_MIUIX_MONET, false),
        keyColor = preferencesStore.getInt(KEY_KEY_COLOR, 0),
        colorStyle = preferencesStore.getString(KEY_COLOR_STYLE, UiPreferences.DEFAULT_COLOR_STYLE)
            ?: UiPreferences.DEFAULT_COLOR_STYLE,
        colorSpec = preferencesStore.getString(KEY_COLOR_SPEC, UiPreferences.DEFAULT_COLOR_SPEC)
            ?: UiPreferences.DEFAULT_COLOR_SPEC,
        pageScale = preferencesStore.getFloat(KEY_PAGE_SCALE, 1.0f),
        enableBlur = preferencesStore.getBoolean(KEY_ENABLE_BLUR, false),
        enableFloatingBottomBar = preferencesStore.getBoolean(KEY_ENABLE_FLOATING_BOTTOM_BAR, false),
        enableFloatingBottomBarBlur = preferencesStore.getBoolean(KEY_ENABLE_FLOATING_BOTTOM_BAR_BLUR, false),
        enableNavigationBadge = preferencesStore.getBoolean(KEY_ENABLE_NAVIGATION_BADGE, true),
        navigationRailExpanded = preferencesStore.getBoolean(KEY_NAVIGATION_RAIL_EXPANDED, false),
        enablePredictiveBack = preferencesStore.getBoolean(KEY_ENABLE_PREDICTIVE_BACK, false),
        enableSwipeDismiss = preferencesStore.getBoolean(KEY_ENABLE_SWIPE_DISMISS, true),
        pagerInterceptionMode = preferencesStore.getInt(KEY_PAGER_INTERCEPTION_MODE, 1).coerceIn(0, 2),
    )

    private companion object {
        const val PREFERENCES_NAME = "settings"
        const val KEY_UI_MODE = "ui_mode"
        const val KEY_CHECK_UPDATE = "check_update"
        const val KEY_THEME_MODE = "color_mode"
        const val KEY_MIUIX_MONET = "miuix_monet"
        const val KEY_KEY_COLOR = "key_color"
        const val KEY_COLOR_STYLE = "color_style"
        const val KEY_COLOR_SPEC = "color_spec"
        const val KEY_PAGE_SCALE = "page_scale"
        const val KEY_ENABLE_BLUR = "enable_blur"
        const val KEY_ENABLE_FLOATING_BOTTOM_BAR = "enable_floating_bottom_bar"
        const val KEY_ENABLE_FLOATING_BOTTOM_BAR_BLUR = "enable_floating_bottom_bar_blur"
        const val KEY_ENABLE_NAVIGATION_BADGE = "enable_navigation_badge"
        const val KEY_NAVIGATION_RAIL_EXPANDED = "nav_rail_expanded"
        const val KEY_ENABLE_PREDICTIVE_BACK = "enable_predictive_back"
        const val KEY_ENABLE_SWIPE_DISMISS = "enable_swipe_dismiss"
        const val KEY_PAGER_INTERCEPTION_MODE = "pager_interception_mode"

        val OBSERVED_KEYS = setOf(
            KEY_UI_MODE,
            KEY_CHECK_UPDATE,
            KEY_THEME_MODE,
            KEY_MIUIX_MONET,
            KEY_KEY_COLOR,
            KEY_COLOR_STYLE,
            KEY_COLOR_SPEC,
            KEY_PAGE_SCALE,
            KEY_ENABLE_BLUR,
            KEY_ENABLE_FLOATING_BOTTOM_BAR,
            KEY_ENABLE_FLOATING_BOTTOM_BAR_BLUR,
            KEY_ENABLE_NAVIGATION_BADGE,
            KEY_NAVIGATION_RAIL_EXPANDED,
            KEY_ENABLE_PREDICTIVE_BACK,
            KEY_ENABLE_SWIPE_DISMISS,
            KEY_PAGER_INTERCEPTION_MODE,
        )
    }
}
