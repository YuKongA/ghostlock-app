package com.ghostlock.app.domain.settings

/**
 * Application-shell preferences consumed by presentation without Android storage knowledge.
 *
 * @property uiMode Persisted Material or Miuix selector.
 * @property checkUpdate Whether the home page checks for manager updates.
 * @property themeMode Persisted color-mode integer.
 * @property miuixMonet Whether Miuix uses the Monet color family.
 * @property keyColor Persisted seed color.
 * @property colorStyle Persisted palette-style name.
 * @property colorSpec Persisted dynamic-color specification name.
 * @property pageScale Presentation density multiplier.
 * @property enableBlur Whether Miuix blur is enabled.
 * @property enableFloatingBottomBar Whether floating bottom chrome is enabled.
 * @property enableFloatingBottomBarBlur Whether floating chrome uses blur.
 * @property enableNavigationBadge Whether navigation badges are enabled.
 * @property navigationRailExpanded Whether wide-screen navigation chrome is expanded.
 * @property enablePredictiveBack Whether the activity opts into Android predictive back.
 * @property enableSwipeDismiss Whether page swipe-dismiss is enabled.
 * @property pagerInterceptionMode Persisted pager interception ordinal.
 * @note Defaults preserve the existing SharedPreferences behavior and contain no Android types.
 */
data class UiPreferences(
    val uiMode: String = DEFAULT_UI_MODE,
    val checkUpdate: Boolean = true,
    val themeMode: Int = DEFAULT_THEME_MODE,
    val miuixMonet: Boolean = false,
    val keyColor: Int = 0,
    val colorStyle: String = DEFAULT_COLOR_STYLE,
    val colorSpec: String = DEFAULT_COLOR_SPEC,
    val pageScale: Float = 1.0f,
    val enableBlur: Boolean = false,
    val enableFloatingBottomBar: Boolean = false,
    val enableFloatingBottomBarBlur: Boolean = false,
    val enableNavigationBadge: Boolean = true,
    val navigationRailExpanded: Boolean = false,
    val enablePredictiveBack: Boolean = false,
    val enableSwipeDismiss: Boolean = true,
    val pagerInterceptionMode: Int = 1,
) {
    companion object {
        const val DEFAULT_UI_MODE = "miuix"
        const val DEFAULT_THEME_MODE = 0
        const val DEFAULT_COLOR_STYLE = "TonalSpot"
        const val DEFAULT_COLOR_SPEC = "SPEC_2025"
    }
}
