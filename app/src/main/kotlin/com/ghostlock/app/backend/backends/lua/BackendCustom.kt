package com.ghostlock.app.backend.backends.lua

import android.content.Intent
import android.net.Uri
import android.provider.OpenableColumns
import com.ghostlock.app.backend.declarations.UnfinishedBackend
import com.ghostlock.app.backend.declarations.BackendRow
import com.ghostlock.app.backend.declarations.BackendSpec
import com.ghostlock.app.R
import com.ghostlock.app.backend.declarations.BackendConfigKey
import com.ghostlock.app.backend.declarations.BackendConfigSchema
import com.ghostlock.app.backend.declarations.BackendConfigStore
import com.ghostlock.app.backend.declarations.BackendConfigType
import com.ghostlock.app.backend.declarations.BackendConfigurationSpec
import com.ghostlock.app.backend.declarations.BackendReadiness
import com.ghostlock.app.backend.declarations.BackendRunCapability
import com.ghostlock.app.backend.declarations.ReadinessReason
import com.ghostlock.app.domain.backend.BackendConfigMutationResult
import com.ghostlock.app.domain.backend.BackendConfigValue
import com.ghostlock.app.domain.backend.BackendConfiguration
import com.ghostlock.app.domain.backend.BackendConfigurationMutation

/**
 *  「自定义」backend（2026-10-18 用户裁定）。
 *
 *  它没有固定的 CVE 与配置项：presentation renderer 负责加载 Lua 脚本的配置区。
 */
@UnfinishedBackend(reason = "Lua/custom backend unfinished")
object BackendCustom : BackendSpec {
    private val scriptMimeTypes =
        listOf("text/x-lua", "text/plain", "application/octet-stream")

    /** Keep the custom Lua backend visible in the selector but disabled in the exec-glue build. */
    override val selectable: Boolean = false
    override val cve: String = "CUSTOM"
    override val titleRes: Int = R.string.backend_custom

    override fun rows(): List<BackendRow> = listOf(
        BackendRow(R.string.backend_row_id, "自定义"),
        BackendRow(R.string.backend_row_status, "未接线"),
    )

    /** 自定义 backend 没有 Flow run 入口；运行页只展示脚本/Lua 能力与拒绝原因。 */

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

    /** Standard document configuration capability for the custom Lua script selection. */
    override val configurationSpec: BackendConfigurationSpec = object : BackendConfigurationSpec {
        /** @return Complete document-picker semantics and current bounded script facts. */
        override fun snapshot(store: BackendConfigStore): BackendConfiguration {
            val selection = LuaScriptSelectionStore.state.value
            return BackendConfiguration.Document(
                key = key,
                field = CONFIG_KEY_SCRIPT_URI,
                importTitle = store.context.getString(R.string.backend_custom_script),
                documentSummary = selection.uri?.let { uri -> displayName(store.context, uri) }
                    ?: store.context.getString(R.string.backend_custom_none),
                mimeTypes = scriptMimeTypes,
                facts = listOf(
                    BackendConfigValue(
                        label = store.context.getString(R.string.backend_custom_runtime),
                        value = LuaScriptHost.runtimeName,
                    ),
                ),
                details = selection.summary,
            )
        }

        /** Validates, commits, and then publishes one Lua document selection. */
        override fun mutate(
            store: BackendConfigStore,
            mutation: BackendConfigurationMutation,
        ): BackendConfigMutationResult {
            if (mutation !is BackendConfigurationMutation.ImportDocument ||
                mutation.field != CONFIG_KEY_SCRIPT_URI
            ) {
                return BackendConfigMutationResult.UNSUPPORTED
            }
            if (mutation.uri.isBlank()) return BackendConfigMutationResult.INVALID
            val parsed = Uri.parse(mutation.uri)
            if (parsed.scheme.isNullOrBlank()) return BackendConfigMutationResult.INVALID
            store.context.contentResolver.takePersistableUriPermission(
                parsed,
                Intent.FLAG_GRANT_READ_URI_PERMISSION,
            )
            val summary = LuaScriptHost.readSummary(store.context, parsed)
            store.putString(CONFIG_KEY_SCRIPT_URI, parsed.toString())
            LuaScriptSelectionStore.selectValidated(parsed, summary)
            return BackendConfigMutationResult.APPLIED
        }
    }

    /**
     * 把当前选中的脚本 URI 写入框架 store。
     *
     * @param store Outer data adapter 注入的 typed store。
     */
    override fun saveConfig(store: BackendConfigStore) {
        store.putString(
            CONFIG_KEY_SCRIPT_URI,
            LuaScriptSelectionStore.state.value.uri?.toString().orEmpty(),
        )
    }

    /**
     * 从框架 store 恢复脚本选择（URI 失效/不可读 ⇒ fail-closed 清空为“未选择”）。
     *
     * @param store Outer data adapter 注入的 typed store。
     */
    override fun restoreConfig(store: BackendConfigStore) {
        LuaScriptSelectionStore.restore(store.context, store.getString(CONFIG_KEY_SCRIPT_URI))
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
     * 且 Lua 运行时未接线；目标环境尚未验证。
     *
     * @return 恒 NotReady，带具名原因。
     */
    /** 脚本 URI 的 snake_case key（命名空间由框架拼）。 */
    private const val CONFIG_KEY_SCRIPT_URI = "script_uri"

    override fun readiness(): BackendReadiness {
        return BackendReadiness.NotReady(
            listOf(
                ReadinessReason(R.string.backend_readiness_custom_script),
                ReadinessReason(R.string.backend_readiness_environment_unverified),
            ),
        )
    }

    /** @return Provider display name or a stable URI path fallback. */
    private fun displayName(context: android.content.Context, uri: Uri): String =
        context.contentResolver.query(uri, null, null, null, null)?.use { cursor ->
            val index = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME)
            if (index >= 0 && cursor.moveToFirst()) cursor.getString(index) else null
        } ?: uri.lastPathSegment ?: "script.lua"
}
