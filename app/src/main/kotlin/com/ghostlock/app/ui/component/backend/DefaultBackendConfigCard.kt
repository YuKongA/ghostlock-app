package com.ghostlock.app.ui.component.backend

import androidx.compose.runtime.Composable
import com.ghostlock.app.domain.backend.BackendConfigValue
import com.ghostlock.app.ui.kit.ConfigItem
import com.ghostlock.app.ui.kit.LocalUiKit

/**
 * backend 的默认配置卡（B1–B4 registry 统一）。
 *
 * 只渲染外层适配器提供的静态行，不做业务分支；定制卡由 presentation 的
 * registry renderer 绑定，backend declaration 不拥有 Composable。
 *
 * @param values 目标 backend 已解析的只读行。
 * @note 无 rows 时渲染空 [Column]（不声明 readiness，避免第二权威）。
 * @note 阶段：backend registry 统一（B1）。
 */
@Composable
fun DefaultBackendConfigCard(values: List<BackendConfigValue>) {
    LocalUiKit.current.ConfigItems(
        values.map { row ->
            ConfigItem.ReadOnly(
                title = row.label,
                summary = row.value,
            )
        },
    )
}
