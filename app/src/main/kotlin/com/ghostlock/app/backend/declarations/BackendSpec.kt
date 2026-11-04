package com.ghostlock.app.backend.declarations

import android.content.Context
import androidx.annotation.StringRes
import com.ghostlock.app.R
import com.ghostlock.app.core.flow.FlowSymbol
import com.ghostlock.app.backend.registry.Backends
import com.ghostlock.app.domain.backend.BackendRuntimeSpec

/**
 *  一个 backend 的声明（2026-10-18 用户裁定：每个 backend 一个配置文件）。
 *
 *  配置页只认这个接口：下拉框显示 [displayNameRes]，下面按 [rows] 渲染配置项。
 *  新增 backend = 新建一个实现本接口的 .kt 文件并加进 [Backends.all]，界面代码不用改。
 */
interface BackendSpec {
    /**
     * Optional standard interactive-configuration capability.
     *
     * @note Null means [rows] is projected as read-only static facts by the outer adapter.
     */
    val configurationSpec: BackendConfigurationSpec?
        get() = null

    /**
     * 运行中是否允许用户中止（缺省 true）。true 的中止当前只是页面视觉状态；
     * false（不可中止 backend）时中止按钮不生效——触发后不得 kill/timeout payload。
     */
    val abortable: Boolean
        get() = true

    /**
     * 执行体 uid 需求声明（只声明；UI 启动前检查/拒绝，backend 不自检）。
     *
     * @note 默认 ANY_UID（App/Shizuku 攻击线程都可启动 lua/43499/43284）。
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

    /** Selector label; glued variants override this so sibling CVEs stay distinct. */
    @get:StringRes
    val displayNameRes: Int
        get() = titleRes

    /** Whether this backend declares that it can be chosen in this build. Disabled by default. */
    val selectable: Boolean
        get() = false

    /**
     * Projects this outer plugin declaration into the immutable domain catalog contract.
     *
     * @param resolveString Composition-root localization boundary for Android string resources.
     * @return Fresh runtime facts; this method does not register, cache, or persist the projection.
     */
    fun runtimeSpec(resolveString: (Int) -> String): BackendRuntimeSpec {
        val currentReadiness = readiness()
        return BackendRuntimeSpec(
            key = key,
            cveLabel = cve,
            displayName = resolveString(displayNameRes),
            statusText = resolveString(statusTextRes()),
            isReady = currentReadiness.isReady,
            selectable = selectable,
        )
    }

    /** 这个 backend 的配置项（label 资源 + 已格式化的值）。 */
    fun rows(): List<BackendRow>

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
     * 新进程 Flow 通道的反射符号（B4a/B5c-1；`null`=该 backend 无 Flow 运行入口）。
     *
     * @note B5c-1 起这是**唯一**执行入口：dispatcher 只走新进程 launcher，不再有 in-process runner。
     */
    val flowSymbol: FlowSymbol?
        get() = null

    /**
     * 构造新进程 Flow 的调用参数（B4a；默认空参数）。
     *
     * @param context UI 进程 context（禁止跨进程传递）。
     * @return Ready（业务参数，不含 `host.*`）或 Refused（具名原因，不 fallback）。
     */
    fun flowParams(context: Context): BackendFlowParams = BackendFlowParams.Ready(emptyMap())

    /**
     * 参数/偏移声明（只声明元数据；值由 profile/debug 配置提供）。
     *
     * @note 供 UI/动态 Lua spec 声明自己需要的内核字段；backend 不自检运行时值。
     */
    val parameterSchema: BackendParameterSchema
        get() = BackendParameterSchema.EMPTY

    /** 是否具备可执行入口：`flowSymbol != null` ⇒ FLOW_RUN（B5c-1 唯一派生源）。 */
    val runCapability: BackendRunCapability
        get() = if (flowSymbol != null) {
            BackendRunCapability.FLOW_RUN
        } else {
            BackendRunCapability.NO_FLOW_RUN
        }

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

/**
 * backend 注册表（**动态**）：内置 backend 固定，Lua 等运行时 spec 可注册/注销。
 *
 * - UI 通过 [all] 观察列表（Compose state）；
 * - 动态 backend 必须提供稳定唯一 [BackendSpec.key] 与（可选）[BackendSpec.flowSymbol]；
 * - 每个 Lua 脚本可声明自己的 `uidRequirement`（ANY/SHELL），由 UI Gate 检查；
 * - 注册/注销不改内置项顺序。
 */
