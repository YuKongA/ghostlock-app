package com.ghostlock.app.ui.screen.module.backends

import androidx.annotation.StringRes
import androidx.compose.runtime.Composable
import com.ghostlock.app.R
import com.ghostlock.app.backend.BackendId
import com.ghostlock.app.ui.backend.BackendUidRequirement
import com.ghostlock.app.ui.backend.BackendFlowRunner
import com.ghostlock.app.ui.backend.BackendParameterSchema
import com.ghostlock.app.ui.backend.BackendConfigSchema
import com.ghostlock.app.ui.backend.BackendConfigStore
import com.ghostlock.app.ui.backend.BackendReadiness
import com.ghostlock.app.ui.backend.BackendRunCapability
import com.ghostlock.app.ui.backend.ReadinessReason
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue

/**
 *  一个 backend 的声明（2026-10-18 用户裁定：每个 backend 一个配置文件）。
 *
 *  配置页只认这个接口：下拉框显示 [titleRes]（CVE 编号），下面按 [rows] 渲染配置项。
 *  新增 backend = 新建一个实现本接口的 .kt 文件并加进 [Backends.all]，界面代码不用改。
 */
interface BackendSpec {
    /**
     * 身份权威（唯一）：dispatcher 按本 id 穷尽 `when`；可为 null 的只有「无 Flow 入口」的
     * 自定义 backend（其身份仍是自身对象）。
     */
    val id: BackendId?
        get() = null

    /**
     * 运行中是否允许用户中止（缺省 true）。true 的中止当前只是页面视觉状态；
     * false（98260）时中止按钮不生效——触发后不得 kill/timeout payload。
     */
    val abortable: Boolean
        get() = true

    /**
     * 执行体 uid 需求声明（只声明；UI 启动前检查/拒绝，backend 不自检）。
     *
     * @note 默认 ANY_UID（App/Shizuku 攻击线程都可启动 lua/43499/43284）；98260→SHELL_UID_2000。
     *   Lua 绕过不在 backend 兜底。
     */
    val uidRequirement: BackendUidRequirement
        get() = BackendUidRequirement.ANY_UID

    /**
     * 动态 backend 的稳定唯一键（注册表/选择/runner 用）。
     *
     * @note 内置 backend 默认用 [cve]；Lua 等动态 spec 必须覆盖为脚本级唯一键
     *   （如 `lua:<scriptId>`），这样同一 JVM 可注册多个脚本 backend。
     */
    val key: String
        get() = cve

    /** CVE 编号（**显示/持久化键**，不是身份权威），例如 "CVE-2026-43284"。 */
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

    /**
     * backend 的 run 入口（R1a 单一权威）：**无默认值**，每个 spec 必须显式声明
     * （无入口写 null）⇒ 新增 backend 漏实现直接编译失败。
     *
     * @note 可运行能力由本属性派生，不再单独声明（见 [runCapability]）。
     */
    val runner: BackendFlowRunner?

    /**
     * 参数/偏移声明（只声明元数据；值由 profile/debug 配置提供）。
     *
     * @note 供 UI/动态 Lua spec 声明自己需要的内核字段；backend 不自检运行时值。
     */
    val parameterSchema: BackendParameterSchema
        get() = BackendParameterSchema.EMPTY

    /** 是否具备可执行入口：由 [runner] 单一派生（有 runner=FLOW_RUN）。 */
    val runCapability: BackendRunCapability
        get() = if (runner != null) BackendRunCapability.FLOW_RUN else BackendRunCapability.NO_FLOW_RUN

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
    /** 选中的 backend 稳定 key（动态注册/移除后仍保持选择）。 */
    var selectedKey: String? by mutableStateOf(null)

    /** @return 当前 key 在 [Backends.all] 的下标；找不到回退 0。 */
    var index: Int
        get() {
            val key = selectedKey
            val found = Backends.all.indexOfFirst { it.key == key }
            return if (found >= 0) found else 0
        }
        set(value) {
            selectedKey = Backends.all.getOrNull(value)?.key
        }

    /**
     * @param key backend 稳定 key。
     * @return true=已选中且存在。
     */
    fun select(key: String): Boolean {
        if (Backends.all.none { it.key == key }) return false
        selectedKey = key
        return true
    }
}

/**
 * backend 注册表（**动态**）：内置 backend 固定，Lua 等运行时 spec 可注册/注销。
 *
 * - UI 通过 [all] 观察列表（Compose state）；
 * - 动态 backend 必须提供稳定唯一 [BackendSpec.key] 与（可选）[BackendSpec.runner]；
 * - 每个 Lua 脚本可声明自己的 `uidRequirement`（ANY/SHELL），由 UI Gate 检查；
 * - 注册/注销不改内置项顺序。
 */
object Backends {
    private val builtins: List<BackendSpec> = listOf(
        BackendCve202643284,
        BackendCve202698260,
        BackendCve202643499,
        BackendCustom,
    )

    /** 当前全部 backend（内置 + 动态）；顺序=下拉框顺序。 */
    var all: List<BackendSpec> by mutableStateOf(builtins)
        private set

    /**
     * 注册一个动态 backend（如同一个 Lua 脚本）。
     *
     * @param spec backend 声明。
     * @return true=注册成功；key 冲突返回 false（不覆盖）。
     */
    fun register(spec: BackendSpec): Boolean {
        if (all.any { it.key == spec.key }) return false
        all = all + spec
        return true
    }

    /**
     * 注销动态 backend（内置不可注销）。
     *
     * @param key 稳定 key。
     * @return true=已移除。
     */
    fun unregister(key: String): Boolean {
        if (builtins.any { it.key == key }) return false
        val next = all.filterNot { it.key == key }
        if (next.size == all.size) return false
        all = next
        if (BackendSelection.selectedKey == key) {
            BackendSelection.selectedKey = next.firstOrNull()?.key
        }
        return true
    }

    /** @return 稳定 key 对应的 spec；不存在 null。 */
    fun byKey(key: String?): BackendSpec? = all.firstOrNull { it.key == key }

    /** 恢复内置列表（测试/诊断用）。 */
    fun resetToBuiltins() {
        all = builtins
        if (BackendSelection.selectedKey?.let { key -> builtins.any { it.key == key } } != true) {
            BackendSelection.selectedKey = builtins.firstOrNull()?.key
        }
    }
}
