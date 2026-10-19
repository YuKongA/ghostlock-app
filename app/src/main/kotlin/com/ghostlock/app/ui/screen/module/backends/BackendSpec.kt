package com.ghostlock.app.ui.screen.module.backends

import androidx.annotation.StringRes
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.setValue

/**
 *  一个 backend 的声明（2026-10-18 用户裁定：每个 backend 一个配置文件）。
 *
 *  配置页只认这个接口：下拉框显示 [titleRes]（CVE 编号），下面按 [rows] 渲染配置项。
 *  新增 backend = 新建一个实现本接口的 .kt 文件并加进 [Backends.all]，界面代码不用改。
 */
interface BackendSpec {
    /** CVE 编号，例如 "CVE-2026-43284"。 */
    val cve: String

    /** 下拉框里显示的名字（CVE 字符串资源）。 */
    @get:StringRes
    val titleRes: Int

    /** 这个 backend 的配置项（label 资源 + 已格式化的值）。 */
    fun rows(): List<BackendRow>

    /** 自定义配置页（可选）：返回非 null 时，界面用这个 composable 代替 rows()。 */
    val customConfig: (@Composable (Boolean) -> Unit)? get() = null
}

/** 一行配置：label 用资源，value 已格式化。 */
data class BackendRow(@param:StringRes val label: Int, val value: String)

/** backend 注册表（顺序即下拉框顺序）。 */
/** 当前选中的 backend（路径页下拉框与主页 badge 共用）。 */
object BackendSelection {
    var index by mutableIntStateOf(0)
}

object Backends {
    val all: List<BackendSpec> = listOf(
        BackendCve202643284,
        BackendCve202643499,
        BackendCustom,
    )
}
