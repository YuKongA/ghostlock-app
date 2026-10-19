package com.ghostlock.app.ui.screen.about

import androidx.compose.runtime.Composable
import androidx.lifecycle.compose.dropUnlessResumed
import com.ghostlock.app.ui.LocalUiMode
import com.ghostlock.app.ui.UiMode
import com.ghostlock.app.ui.navigation3.LocalNavigator

/** 关于页：布局按 remote/main 的 AboutUI 搬过来。 */
@Composable
fun AboutScreen() {
    val navigator = LocalNavigator.current
    val onBack = dropUnlessResumed { navigator.pop() }
    when (LocalUiMode.current) {
        UiMode.Miuix -> AboutScreenMiuix(onBack)
        UiMode.Material -> AboutScreenMaterial(onBack)
    }
}
