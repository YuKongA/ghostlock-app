package com.ghostlock.app.backend.backends.cve43284

import com.ghostlock.app.backend.declarations.UnfinishedBackend
import com.ghostlock.app.backend.declarations.BackendRow
import com.ghostlock.app.backend.declarations.BackendSpec
import com.ghostlock.app.core.finish.*
import com.ghostlock.app.R
import com.ghostlock.app.backend.declarations.BackendConfigKey
import com.ghostlock.app.backend.declarations.BackendConfigSchema
import com.ghostlock.app.backend.declarations.BackendConfigStore
import com.ghostlock.app.backend.declarations.BackendConfigType
import com.ghostlock.app.backend.declarations.BackendConfigurationSpec
import com.ghostlock.app.backend.declarations.BackendReadiness
import com.ghostlock.app.backend.declarations.ReadinessReason
import com.ghostlock.app.domain.backend.BackendConfigIcon
import com.ghostlock.app.domain.backend.BackendConfigMutationResult
import com.ghostlock.app.domain.backend.BackendConfiguration
import com.ghostlock.app.domain.backend.BackendConfigurationMutation

/**
 *  CVE-2026-43284 的配置页（页缓存写 + LKM relay）。
 *
 *  这里只保留一个 backend 选项：加载 relay 后是否开启内核内存访问接口。
 *  编排与收尾的执行入口统一在「执行」页的三态按钮，不再在配置页放按钮。
 */
@UnfinishedBackend(reason = "original CVE-2026-43284 backend unfinished")
object BackendCve43284 : BackendSpec {
    /** Keep the original backend visible in the selector but disabled in the exec-glue build. */
    override val selectable: Boolean = false

    override val cve: String = "CVE-2026-43284"
    override val titleRes: Int = R.string.backend_cve_2026_43284

    override fun rows(): List<BackendRow> = listOf(
        BackendRow(R.string.backend_row_id, cve),
        BackendRow(R.string.backend_row_status, "无需配置"),
    )

    /** 本 backend 没有 Flow run 入口：运行页只展示 Finish/LKM relay 能力。 */

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

    /** Standard configuration capability; normal reads and writes use canonical [FinishConfig]. */
    override val configurationSpec: BackendConfigurationSpec = object : BackendConfigurationSpec {
        /** @return Complete renderer semantics for the relay-access toggle. */
        override fun snapshot(store: BackendConfigStore): BackendConfiguration {
            FinishConfig.load(store.context)
            return BackendConfiguration.Toggle(
                key = key,
                field = CONFIG_KEY_KERNEL_ACCESS,
                title = store.context.getString(R.string.backend_43284_access_title),
                enabled = FinishConfig.state.kernelAccessAfterRelay,
                enabledSummary = store.context.getString(R.string.backend_43284_access_on),
                disabledSummary = store.context.getString(R.string.backend_43284_access_off),
                icon = BackendConfigIcon.SECURITY,
            )
        }

        /** Applies only this backend's declared boolean field through [FinishConfig]. */
        override fun mutate(
            store: BackendConfigStore,
            mutation: BackendConfigurationMutation,
        ): BackendConfigMutationResult {
            if (mutation !is BackendConfigurationMutation.SetBoolean ||
                mutation.field != CONFIG_KEY_KERNEL_ACCESS
            ) {
                return BackendConfigMutationResult.UNSUPPORTED
            }
            FinishConfig.load(store.context)
            FinishConfig.update(
                store.context,
                FinishConfig.state.copy(kernelAccessAfterRelay = mutation.value),
            )
            return BackendConfigMutationResult.APPLIED
        }
    }

    /**
     * 保留 session lifecycle hook；canonical [FinishConfig] 不回写 retired typed duplicate。
     *
     * @param store Outer data adapter 注入的 typed store；本 hook 有意不写入。
     */
    override fun saveConfig(store: BackendConfigStore) {
        /* FinishConfig is canonical; never recreate the retired typed duplicate. */
    }

    /**
     * 读框架 store 并写回 `FinishConfig`（启动恢复/切换 backend 时调用）。
     *
     * @param store Outer data adapter 注入的 typed store，只作一次性迁移输入。
     * @note 幂等；先 load 再 copy，避免丢其它收尾字段。
     */
    override fun restoreConfig(store: BackendConfigStore) {
        FinishConfig.load(store.context)
        if (store.contains(CONFIG_KEY_KERNEL_ACCESS)) {
            val migrated = store.getBoolean(CONFIG_KEY_KERNEL_ACCESS)
            FinishConfig.update(
                store.context,
                FinishConfig.state.copy(kernelAccessAfterRelay = migrated),
            )
            store.clear()
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
     * 真实来源：无 Flow 运行入口（只有 Finish/LKM relay）且目标环境尚未验证。
     *
     * @return 恒 NotReady，至少两条具名原因。
     */
    override fun readiness(): BackendReadiness {
        return BackendReadiness.NotReady(
            listOf(
                ReadinessReason(R.string.backend_run_no_flow_entry),
                ReadinessReason(R.string.backend_readiness_environment_unverified),
            ),
        )
    }

    /** 内核访问开关的 snake_case key（命名空间由框架拼）。 */
    private const val CONFIG_KEY_KERNEL_ACCESS = "kernel_access_after_relay"
}
