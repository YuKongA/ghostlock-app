package com.ghostlock.app.ui.screen.module.backends

import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Security
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.ui.Modifier
import com.ghostlock.app.ui.component.material.SegmentedColumn
import com.ghostlock.app.ui.component.material.SegmentedListItem
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import com.ghostlock.app.R
import top.yukonga.miuix.kmp.preference.SwitchPreference

/**
 *  CVE-2026-43284 的配置页（页缓存写 + LKM relay）。
 *
 *  这里只保留一个 backend 选项：加载 relay 后是否开启内核内存访问接口。
 *  编排与收尾的执行入口统一在「执行」页的三态按钮，不再在配置页放按钮。
 */
object BackendCve202643284 : BackendSpec {
    override val cve: String = "CVE-2026-43284"
    override val titleRes: Int = R.string.backend_cve_2026_43284

    override fun rows(): List<BackendRow> = listOf(
        BackendRow(R.string.backend_row_id, cve),
        BackendRow(R.string.backend_row_status, "无需配置"),
    )

    override val customConfig: (@Composable (Boolean) -> Unit) = { miuix ->
        Backend43284Config(miuix)
    }
}

@Composable
private fun Backend43284Config(miuix: Boolean) {
    val context = androidx.compose.ui.platform.LocalContext.current
    val enabled = FinishConfig.state.kernelAccessAfterRelay
    val title = androidx.compose.ui.res.stringResource(R.string.backend_43284_access_title)
    val summaryOn = androidx.compose.ui.res.stringResource(R.string.backend_43284_access_on)
    val summaryOff = androidx.compose.ui.res.stringResource(R.string.backend_43284_access_off)
    val summary = if (enabled) summaryOn else summaryOff

    fun toggle(value: Boolean) {
        FinishConfig.update(context, FinishConfig.state.copy(kernelAccessAfterRelay = value))
    }

    if (miuix) {
        SwitchPreference(
            startAction = {
                androidx.compose.material3.Icon(
                    imageVector = Icons.Filled.Security,
                    contentDescription = null,
                    tint = top.yukonga.miuix.kmp.theme.MiuixTheme.colorScheme.onBackground,
                )
            },
            title = title,
            summary = summary,
            checked = enabled,
            onCheckedChange = ::toggle,
        )
    } else {
        SegmentedColumn(modifier = Modifier.fillMaxWidth()) {
            item {
                SegmentedListItem(
                    headlineContent = { Text(title) },
                    supportingContent = { Text(summary) },
                    trailingContent = { Switch(checked = enabled, onCheckedChange = ::toggle) },
                )
            }
        }
    }
}
