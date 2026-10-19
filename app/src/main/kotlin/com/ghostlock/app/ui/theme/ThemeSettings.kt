package com.ghostlock.app.ui.theme

import android.content.Context
import com.materialkolor.PaletteStyle

/**
 * 主题模式：跟随系统 / 强制浅色 / 强制深色（对应 KernelSU 的 themeMode）。
 *
 * 机械改动（2026-10-18 恢复移植）：KernelSU 的 ColorMode（7 个取值）已并入同一包，
 * 这里改名为 ThemeMode 以消除顶层声明冲突，语义不变。
 */
enum class ThemeMode { SYSTEM, LIGHT, DARK }

/**
 *  主题设置（2026-10-18 从 KernelSU 设置页搬过来的“自定义主题”选项）。
 *
 *  @property colorMode 主题模式。
 *  @property dynamic 动态取色（Android 12+ 从壁纸取种子色）。
 *  @property keyColor 自定义种子色（0 = 不用，配合 dynamic=false）。
 *  @property paletteStyle MaterialKolor 的配色风格（TonalSpot / Vibrant / Expressive…）。
 *  @property useMiuix 套件：false = Material，true = miuix（KernelSU 风格）。
 */
data class ThemeSettings(
    val colorMode: ThemeMode = ThemeMode.SYSTEM,
    val dynamic: Boolean = true,
    val keyColor: Int = 0,
    val paletteStyle: PaletteStyle = PaletteStyle.TonalSpot,
    val useMiuix: Boolean = false,
)

/** 主题设置持久化（SharedPreferences）。 */
class ThemeSettingsStore(context: Context) {
    private val prefs = context.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)

    fun read(): ThemeSettings {
        val mode = runCatching { ThemeMode.valueOf(prefs.getString(KEY_MODE, null) ?: "") }
            .getOrDefault(ThemeMode.SYSTEM)
        val style = runCatching { PaletteStyle.valueOf(prefs.getString(KEY_STYLE, null) ?: "") }
            .getOrDefault(PaletteStyle.TonalSpot)
        return ThemeSettings(
            colorMode = mode,
            dynamic = prefs.getBoolean(KEY_DYNAMIC, true),
            keyColor = prefs.getInt(KEY_COLOR, 0),
            paletteStyle = style,
            useMiuix = prefs.getBoolean(KEY_MIUIX, false),
        )
    }

    fun write(settings: ThemeSettings) {
        prefs.edit()
            .putString(KEY_MODE, settings.colorMode.name)
            .putBoolean(KEY_DYNAMIC, settings.dynamic)
            .putInt(KEY_COLOR, settings.keyColor)
            .putString(KEY_STYLE, settings.paletteStyle.name)
            .putBoolean(KEY_MIUIX, settings.useMiuix)
            .apply()
    }

    private companion object {
        const val PREFS = "ghostlock.theme"
        const val KEY_MODE = "theme.mode"
        const val KEY_DYNAMIC = "theme.dynamic"
        const val KEY_COLOR = "theme.color"
        const val KEY_STYLE = "theme.style"
        const val KEY_MIUIX = "theme.miuix"
    }
}
