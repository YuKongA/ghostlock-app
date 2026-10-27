package com.ghostlock.app.ui.screen.module.backends

import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Security
import androidx.compose.material3.Icon
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
import com.ghostlock.app.ui.Natives
import com.ghostlock.app.ui.backend.BackendConfigKey
import com.ghostlock.app.ui.backend.BackendConfigSchema
import com.ghostlock.app.ui.backend.BackendConfigStore
import com.ghostlock.app.ui.backend.BackendConfigType
import com.ghostlock.app.ui.backend.BackendReadiness
import com.ghostlock.app.ui.backend.BackendRunCapability
import com.ghostlock.app.ui.backend.BackendSessionStore
import com.ghostlock.app.ui.backend.ReadinessReason
import com.ghostlock.app.ui.util.rootAvailable
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

    /** 本 backend 没有 Flow run 入口：运行页只展示 Finish/LKM relay 能力。 */
    override val runCapability: BackendRunCapability =
        BackendRunCapability.NO_FLOW_RUN

    /**
     * 本 backend 唯一持久化配置：中继 LKM 后开启内核内存访问（默认 false）。
     *
     * @note key 为稳定 snake_case，命名空间由框架拼成 `CVE-2026-43284:kernel_access_after_relay`。
     */
    override val configSchema: BackendConfigSchema = BackendConfigSchema(
        listOf(
            BackendConfigKey(
                CONFIG_KEY_KERNEL_ACCESS,
                BackendConfigType.BOOLEAN,
                false,
            ),
        ),
    )

    /**
     * 从 `FinishConfig.state` 写内核访问开关到框架 store（先加载已保存的 FinishState，避免覆盖）。
     *
     * @param store UI 框架注入的配置 store。
     */
    override fun saveConfig(store: BackendConfigStore) {
        FinishConfig.load(store.context)
        store.putBoolean(CONFIG_KEY_KERNEL_ACCESS, FinishConfig.state.kernelAccessAfterRelay)
    }

    /**
     * 读框架 store 并写回 `FinishConfig`（启动恢复/切换 backend 时调用）。
     *
     * @param store UI 框架注入的配置 store。
     * @note 幂等；先 load 再 copy，避免丢其它收尾字段。
     */
    override fun restoreConfig(store: BackendConfigStore) {
        FinishConfig.load(store.context)
        if (store.contains(CONFIG_KEY_KERNEL_ACCESS)) {
            val enabled = store.getBoolean(CONFIG_KEY_KERNEL_ACCESS)
            FinishConfig.update(
                store.context,
                FinishConfig.state.copy(kernelAccessAfterRelay = enabled),
            )
        } else {
            /* 首次升级（旧版本只写 FinishConfig）：以 legacy 值为准并回填 schema。 */
            store.putBoolean(CONFIG_KEY_KERNEL_ACCESS, FinishConfig.state.kernelAccessAfterRelay)
        }
    }

    /** backend 提供短文案：就绪 → 「就绪」；当前 → 「未就绪：无运行入口」。 */
    override fun statusTextRes(): Int =
        if (readiness().isReady) {
            R.string.backend_readiness_ready
        } else {
            R.string.backend_status_cve43284_not_ready
        }

    /**
     * 真实来源：无 Flow 运行入口（只有 Finish/LKM relay）+ 环境未验证（Natives/root 为 stub）。
     *
     * @return 恒 NotReady，至少两条具名原因。
     */
    override fun readiness(): BackendReadiness {
        val rootDetail = "isManager=" + Natives.isManager +
            ", rootAvailable=" + rootAvailable()
        return BackendReadiness.NotReady(
            listOf(
                ReadinessReason(R.string.backend_run_no_flow_entry),
                ReadinessReason(R.string.backend_readiness_unverified_env, rootDetail),
            ),
        )
    }

    override val customConfig: (@Composable (Boolean) -> Unit) = { miuix ->
        Backend43284Config(miuix)
    }

    /** 内核访问开关的 snake_case key（命名空间由框架拼）。 */
    private const val CONFIG_KEY_KERNEL_ACCESS = "kernel_access_after_relay"
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
        /* task-27：开关变更同时经 backend save 持久化到框架 store（重启恢复）。 */
        runCatching {
            BackendCve202643284.saveConfig(
                BackendSessionStore.from(context).configStore(context, BackendCve202643284),
            )
        }
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
                    leadingContent = { Icon(Icons.Filled.Security, contentDescription = null) },
                    headlineContent = { Text(title) },
                    supportingContent = { Text(summary) },
                    trailingContent = { Switch(checked = enabled, onCheckedChange = ::toggle) },
                )
            }
        }
    }
}
