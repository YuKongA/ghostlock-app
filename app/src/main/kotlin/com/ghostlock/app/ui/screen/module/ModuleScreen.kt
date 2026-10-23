package com.ghostlock.app.ui.screen.module

import androidx.compose.runtime.Composable
import androidx.compose.ui.unit.Dp
import com.ghostlock.app.ui.LocalUiMode
import com.ghostlock.app.ui.UiMode

/** 「路径」页：选择 backend 并渲染它的配置。 */
@Composable
fun BackendPager(
    bottomInnerPadding: Dp,
    isCurrentPage: Boolean = true,
) {
    /* 2026-10-18 用户裁定：本页改为 backend 选择 + 设定，不再有搜索框与模块卡片。 */
    when (LocalUiMode.current) {
        UiMode.Material -> BackendPageMaterial(bottomInnerPadding)
        UiMode.Miuix -> BackendPageMiuix(bottomInnerPadding)
    }
}
