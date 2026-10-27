package com.ghostlock.app.ui.screen.module.backends

import androidx.compose.runtime.Composable
import com.ghostlock.app.R
import com.ghostlock.app.ui.Natives
import com.ghostlock.app.ui.backend.BackendConfigKey
import com.ghostlock.app.ui.backend.BackendConfigSchema
import com.ghostlock.app.ui.backend.BackendConfigStore
import com.ghostlock.app.ui.backend.BackendConfigType
import com.ghostlock.app.ui.backend.BackendReadiness
import com.ghostlock.app.ui.backend.BackendRunCapability
import com.ghostlock.app.ui.backend.ReadinessReason
import com.ghostlock.app.ui.util.rootAvailable

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

    /** 自定义 backend 没有 Flow run 入口；运行页只展示脚本/Lua 能力与拒绝原因。 */
    override val runCapability: BackendRunCapability =
        BackendRunCapability.NO_FLOW_RUN

    /**
     * 唯一持久化配置：选中的 Lua 脚本 URI（默认空串 = 未选择）。
     *
     * @note 只存 URI 字符串（不含脚本内容/身份密钥）；命名空间由框架拼成 `CUSTOM:script_uri`。
     */
    override val configSchema: BackendConfigSchema = BackendConfigSchema(
        listOf(
            BackendConfigKey(CONFIG_KEY_SCRIPT_URI, BackendConfigType.STRING, ""),
        ),
    )

    /**
     * 把当前选中的脚本 URI 写入框架 store。
     *
     * @param store UI 框架注入的配置 store。
     */
    override fun saveConfig(store: BackendConfigStore) {
        store.putString(CONFIG_KEY_SCRIPT_URI, CustomLuaConfigState.uri?.toString().orEmpty())
    }

    /**
     * 从框架 store 恢复脚本选择（URI 失效/不可读 ⇒ fail-closed 清空为“未选择”）。
     *
     * @param store UI 框架注入的配置 store。
     */
    override fun restoreConfig(store: BackendConfigStore) {
        CustomLuaConfigState.restore(store.context, store.getString(CONFIG_KEY_SCRIPT_URI))
    }

    /** backend 提供短文案：就绪 → 「就绪」；当前 → 「未就绪：未选择脚本」。 */
    override fun statusTextRes(): Int =
        if (readiness().isReady) {
            R.string.backend_readiness_ready
        } else {
            R.string.backend_status_custom_not_ready
        }

    /**
     * 真实来源：自定义 Lua 脚本选择状态只存在于 Composable 的 `remember`（无持久化来源），
     * 且 Lua 运行时未接线；环境探测（Natives/root）当前为 stub。
     *
     * @return 恒 NotReady，带具名原因。
     */
    /** 脚本 URI 的 snake_case key（命名空间由框架拼）。 */
    private const val CONFIG_KEY_SCRIPT_URI = "script_uri"

    override fun readiness(): BackendReadiness {
        val rootDetail = "isManager=" + Natives.isManager +
            ", rootAvailable=" + rootAvailable()
        return BackendReadiness.NotReady(
            listOf(
                ReadinessReason(R.string.backend_readiness_custom_script),
                ReadinessReason(R.string.backend_readiness_unverified_env, rootDetail),
            ),
        )
    }
}
