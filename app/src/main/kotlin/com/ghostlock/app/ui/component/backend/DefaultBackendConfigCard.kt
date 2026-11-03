package com.ghostlock.app.ui.component.backend

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import com.ghostlock.app.backend.declarations.BackendSpec
import com.ghostlock.app.ui.component.material.SegmentedColumn
import com.ghostlock.app.ui.component.material.SegmentedListItem

/**
 * backend 的默认配置卡（B1–B4 registry 统一）。
 *
 * 只渲染 [BackendSpec.rows] 的静态行，不做业务分支；需要定制卡的 backend 覆盖
 * [BackendSpec.ConfigCard] 即可（唯一 UI 入口）。
 *
 * @param spec 目标 backend（[BackendSpec.ConfigCard] 的默认实现传入 `this`）。
 * @param miuix true=Miuix 主题；false=Material3。
 * @note 无 rows 时渲染空 [Column]（不声明 readiness，避免第二权威）。
 * @note 阶段：backend registry 统一（B1）。
 */
@Composable
fun DefaultBackendConfigCard(spec: BackendSpec, miuix: Boolean) {
    val rows = spec.rows()
    if (miuix) {
        Column {
            for (row in rows) {
                top.yukonga.miuix.kmp.basic.BasicComponent(
                    title = stringResource(row.label),
                    summary = row.value,
                )
            }
        }
    } else {
        SegmentedColumn(modifier = Modifier.fillMaxWidth()) {
            for (row in rows) {
                item {
                    SegmentedListItem(
                        headlineContent = { Text(stringResource(row.label)) },
                        supportingContent = { Text(row.value) },
                    )
                }
            }
        }
    }
}
