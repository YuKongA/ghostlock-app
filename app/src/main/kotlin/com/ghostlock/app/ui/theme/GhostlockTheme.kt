package com.ghostlock.app.ui.theme

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.dynamicDarkColorScheme
import androidx.compose.material3.dynamicLightColorScheme
import androidx.compose.material3.LocalContentColor
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import com.ghostlock.app.ui.kit.UiKit
import com.materialkolor.dynamiccolor.ColorSpec
import com.materialkolor.rememberDynamicColorScheme
import top.yukonga.miuix.kmp.theme.ColorSchemeMode
import top.yukonga.miuix.kmp.theme.MiuixTheme
import top.yukonga.miuix.kmp.theme.ThemeColorSpec
import top.yukonga.miuix.kmp.theme.ThemeController
import top.yukonga.miuix.kmp.theme.ThemePaletteStyle

/**
 *  主题（2026-10-18 起按 KernelSU 的“自定义主题”模型实现）。
 *
 *  - Material：material-kolor 的 [rememberDynamicColorScheme]（种子色 + paletteStyle），
 *    动态取色时种子色取系统壁纸色；
 *  - miuix：用它的 [ThemeController]（模式 / 种子色 / paletteStyle）包一层；
 *  - 主题模式来自 [ThemeSettings]（跟随系统 / 浅色 / 深色）。
 *
 *  @param kit 套件（Material / miuix）。
 *  @param settings 主题设置。
 *  @param content 界面内容。
 */
@Composable
fun GhostlockTheme(
    kit: UiKit,
    settings: ThemeSettings,
    content: @Composable () -> Unit,
) {
    val context = LocalContext.current
    val systemDark = isSystemInDarkTheme()
    val dark = when (settings.colorMode) {
        ThemeMode.SYSTEM -> systemDark
        ThemeMode.LIGHT -> false
        ThemeMode.DARK -> true
    }

    when (kit) {
        UiKit.Material -> {
            val seed = when {
                settings.dynamic -> {
                    val primary = if (dark) dynamicDarkColorScheme(context).primary
                    else dynamicLightColorScheme(context).primary
                    primary
                }
                else -> Color(settings.keyColor)
            }
            val scheme = rememberDynamicColorScheme(
                seedColor = seed,
                isDark = dark,
                isAmoled = false,
                style = settings.paletteStyle,
                specVersion = ColorSpec.SpecVersion.SPEC_2021,
            )
            MaterialTheme(colorScheme = scheme, content = content)
        }

        UiKit.Miuix -> {
            val style = runCatching { ThemePaletteStyle.valueOf(settings.paletteStyle.name) }
                .getOrDefault(ThemePaletteStyle.TonalSpot)
            val keyColor = when {
                !settings.dynamic -> Color(settings.keyColor)
                else -> null
            }
            val controller = ThemeController(
                when (settings.colorMode) {
                    ThemeMode.SYSTEM -> ColorSchemeMode.System
                    ThemeMode.LIGHT -> ColorSchemeMode.Light
                    ThemeMode.DARK -> ColorSchemeMode.Dark
                },
                keyColor = keyColor,
                isDark = dark,
                paletteStyle = style,
                colorSpec = ThemeColorSpec.Spec2021,
            )
            MiuixTheme(controller = controller) {
                CompositionLocalProvider(
                    LocalContentColor provides top.yukonga.miuix.kmp.theme.MiuixTheme.colorScheme.onBackground,
                    content = content,
                )
            }
        }
    }
}
