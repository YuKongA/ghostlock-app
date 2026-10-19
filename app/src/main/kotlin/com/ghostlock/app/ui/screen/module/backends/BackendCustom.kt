package com.ghostlock.app.ui.screen.module.backends

import androidx.compose.runtime.Composable
import com.ghostlock.app.R

/**
 *  「自定义」backend（2026-10-18 用户裁定）。
 *
 *  它没有固定的 CVE 与配置项：配置区由单独的 [CustomLuaConfig] 负责（加载 Lua 脚本）。
 */
object BackendCustom : BackendSpec {
    override val cve: String = "CUSTOM"
    override val titleRes: Int = R.string.backend_custom

    override fun rows(): List<BackendRow> = listOf(
        BackendRow(R.string.backend_row_id, "自定义"),
        BackendRow(R.string.backend_row_status, "未接线"),
    )

    override val customConfig: (@Composable (Boolean) -> Unit)? = { miuix -> CustomLuaConfig(miuix) }
}
