package com.ghostlock.app.ui.navigation3

import android.os.Parcelable
import kotlinx.parcelize.Parcelize
import kotlinx.serialization.Serializable
import com.ghostlock.app.ui.screen.scriptrun.ScriptRunArg
import com.ghostlock.app.ui.util.ScriptRunArgSerializer
import com.ghostlock.app.ui.util.TemplateInfoSerializer
import com.ghostlock.app.ui.viewmodel.TemplateViewModel
import top.yukonga.miuix.kmp.nav.core.NavKey

/**
 * Type-safe navigation keys for miuix-nav.
 * Each destination is a NavKey (data object/data class) and can be saved/restored in the back stack.
 */
@Serializable
sealed interface Route : NavKey, Parcelable {
    @Parcelize
    @Serializable
    data object Main : Route

    @Parcelize
    @Serializable
    data object Home : Route

    @Parcelize
    @Serializable
    data object SuperUser : Route

    @Parcelize
    @Serializable
    data object Module : Route

    @Parcelize
    @Serializable
    data object Settings : Route

    @Parcelize
    @Serializable
    data object About : Route

    @Parcelize
    @Serializable
    data object Sulog : Route

    @Parcelize
    @Serializable
    data object ColorPalette : Route

    @Parcelize
    @Serializable
    data object AppProfileTemplate : Route

    @Parcelize
    @Serializable
    data class TemplateEditor(
        @Serializable(with = TemplateInfoSerializer::class) val template: TemplateViewModel.TemplateInfo,
        val readOnly: Boolean
    ) : Route

    @Parcelize
    @Serializable
    data class AppProfile(val uid: Int) : Route

    /* 安装/刷机/模块仓库三个功能屏随未接线裁剪一并移除（2026-10-18 恢复移植）；
     * 路由表不再保留 Install / Flash / ModuleRepo / ModuleRepoDetail 分支。 */

    @Parcelize
    @Serializable
    data class ExecuteModuleAction(val moduleId: String, val fromShortcut: Boolean = false) : Route

    @Parcelize
    @Serializable
    data class ScriptRun(@Serializable(with = ScriptRunArgSerializer::class) val arg: ScriptRunArg) : Route
}
