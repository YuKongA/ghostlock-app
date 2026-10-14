package com.ghostlock.app.ui.theme

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme as materialDarkColorScheme
import androidx.compose.material3.lightColorScheme as materialLightColorScheme
import androidx.compose.runtime.Composable
import com.ghostlock.app.ui.kit.UiKit
import top.yukonga.miuix.kmp.theme.MiuixTheme
import top.yukonga.miuix.kmp.theme.darkColorScheme as miuixDarkColorScheme
import top.yukonga.miuix.kmp.theme.lightColorScheme as miuixLightColorScheme

/**
 * 按套件包一层主题：Material 走 Material3 的配色方案，Miuix 走 MiuixTheme。
 *
 * 为什么在这里分派而不是在两个界面里各写一遍：主题是**一处**的事，界面只声明自己是哪个套件。
 * 生产 UI 落地时，新增一个套件 = 这里加一个分支 + GhostlockKit 加一个实现，界面代码不动。
 *
 * 说明：miuix 的 monetSystemColors 是 internal（2026-10-14 编译期实测：Kotlin 报 "it is internal
 * in file"），所以这里用它的公开工厂 light/darkColorScheme()，与本仓锁定的 miuix 版本配套；
 * 系统取色要等 ThemeController 那条路公开化，或改用 MiuixTheme(controller = ...) 时再接。
 */
@Composable
fun GhostlockTheme(kit: UiKit, content: @Composable () -> Unit) {
    val dark = isSystemInDarkTheme()
    when (kit) {
        UiKit.Material -> MaterialTheme(
            colorScheme = if (dark) materialDarkColorScheme() else materialLightColorScheme(),
            content = content,
        )

        UiKit.Miuix -> MiuixTheme(
            colors = if (dark) miuixDarkColorScheme() else miuixLightColorScheme(),
            content = content,
        )
    }
}
