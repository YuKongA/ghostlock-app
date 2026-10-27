package com.ghostlock.app.ui.screen.module.backends

import androidx.annotation.StringRes
import androidx.compose.runtime.Composable
import com.ghostlock.app.R
import com.ghostlock.app.ui.backend.BackendConfigSchema
import com.ghostlock.app.ui.backend.BackendConfigStore
import com.ghostlock.app.ui.backend.BackendReadiness
import com.ghostlock.app.ui.backend.BackendRunCapability
import com.ghostlock.app.ui.backend.ReadinessReason
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

    /**
     * 当前环境下的运行就绪声明（真实来源；**禁止写死 Ready**）。
     *
     * - 只有 [BackendReadiness.Ready] 才可能通过 `BackendRunGate`（还会叠加运行页上下文）；
     * - 默认 NotReady：未显式声明的 backend 一律不可运行（fail-closed）。
     *
     * @return 该 backend 的 readiness 声明。
     */
    fun readiness(): BackendReadiness =
        BackendReadiness.NotReady(
            listOf(ReadinessReason(R.string.backend_readiness_undeclared)),
        )

    /** 是否具备可在运行页执行的 backend run 入口；默认无（fail-closed）。 */
    val runCapability: BackendRunCapability
        get() = BackendRunCapability.NO_FLOW_RUN

    /**
     * backend 自己提供的**完整本地化状态短文案**（UI 框架不拼文本，只按 readiness 选图标/颜色）。
     *
     * @return 状态文案资源：Ready ⇒ 「就绪」；NotReady ⇒ 该 backend 自己的短句原因。
     * @note 默认 fail-closed：未声明的 backend 返回「未声明」短句。
     */
    fun statusTextRes(): Int =
        if (readiness().isReady) {
            R.string.backend_readiness_ready
        } else {
            R.string.backend_readiness_undeclared
        }

    /**
     * 本 backend 允许持久化的配置项（task-27；UI 框架据此白名单校验）。
     *
     * @return 配置 schema；默认空 = 不持久化任何内容（fail-closed）。
     * @note key/类型/默认值规则见 [BackendConfigKey]；backend 不得直接碰 SharedPreferences。
     */
    val configSchema: BackendConfigSchema
        get() = BackendConfigSchema.EMPTY

    /**
     * 把 backend 当前内存配置写入框架 store（具体内容由 backend 决定）。
     *
     * @param store UI 框架创建/注入的配置 store（命名空间/类型白名单已由框架强制）。
     * @note 默认 no-op（fail-closed 兼容）；实现方必须只写 [configSchema] 声明的 key。
     */
    fun saveConfig(store: BackendConfigStore) {
        /* 默认不持久化任何配置；需要持久化的 backend 覆盖本方法。 */
    }

    /**
     * 从框架 store 恢复 backend 配置到自身运行时状态。
     *
     * @param store UI 框架创建/注入的配置 store。
     * @note 默认 no-op（fail-closed 兼容）；实现方必须幂等、无副作用顺序依赖。
     */
    fun restoreConfig(store: BackendConfigStore) {
        /* 默认不恢复任何配置；需要持久化的 backend 覆盖本方法。 */
    }
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
