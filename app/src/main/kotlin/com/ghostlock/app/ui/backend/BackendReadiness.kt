package com.ghostlock.app.ui.backend

import androidx.annotation.StringRes
import androidx.compose.runtime.Composable
import androidx.compose.runtime.Immutable
import androidx.compose.ui.res.stringResource
import com.ghostlock.app.R

/**
 * 一条「未就绪原因」：资源 id + 可选真实取值细节。
 *
 * @property messageRes 中英文案资源；由 UI 统一解析，backend 文件只声明事实。
 * @property detail 可选的原始取值（例如 stub 探测到的 isManager/rootAvailable），用于报告真实性。
 * @note 阶段：task-21 UI 运行就绪框架；本类型是纯声明数据，不含任何执行逻辑。
 */
@Immutable
data class ReadinessReason(
    @param:StringRes val messageRes: Int,
    val detail: String? = null,
)

/**
 * 一个 backend 声明的运行就绪性（UI 框架的唯一事实来源）。
 *
 * 契约：
 * - 各 backend（43284/43499/CUSTOM）必须按**真实来源**声明，禁止写死 [Ready]；
 * - 只有 [Ready] 才可能通过 `BackendRunGate`（还会叠加运行页上下文）；
 * - [NotReady] 必须给出至少一条具名原因，UI 原样展示。
 *
 * @note 阶段：task-21；本类型不决定是否允许运行（那是 `BackendRunGate` 的唯一职责）。
 */
@Immutable
sealed interface BackendReadiness {
    /** 已就绪：可进入 Gate 的上下文判断。 */
    data class Ready(val notes: List<ReadinessReason> = emptyList()) : BackendReadiness

    /** 未就绪：至少一条具名原因（缺前置/环境未验证/物理输入缺失等）。 */
    data class NotReady(val reasons: List<ReadinessReason>) : BackendReadiness

    /** @return 是否 Ready（不叠加运行页上下文）。 */
    val isReady: Boolean get() = this is Ready

    /** @return 需要展示的原因；Ready 时返回 notes，NotReady 时返回 reasons。 */
    val blockingReasons: List<ReadinessReason>
        get() = when (this) {
            is Ready -> notes
            is NotReady -> reasons
        }
}

/**
 * readiness → 视觉语义（UI 框架只按此选图标/颜色，**不拼文本**）。
 *
 * @note 文本由各 backend 的 `BackendSpec.statusTextRes()` 提供。
 */
enum class BackendStatusTone {
    /** 就绪：对勾 + 正常容器色。 */
    READY,

    /** 未就绪：警告图标 + error/warning 容器色。 */
    NOT_READY,
}

/** @return 当前 readiness 对应的视觉语义（仅框架映射用）。 */
val BackendReadiness.statusTone: BackendStatusTone
    get() = if (isReady) BackendStatusTone.READY else BackendStatusTone.NOT_READY

/**
 * backend 的可运行能力声明（运行页据此 + readiness 决定是否给按钮）。
 *
 * @note `FLOW_RUN` 表示该 backend 有可被 UI 框架分发的 run 入口；`NO_FLOW_RUN`
 *   表示只提供配置/收尾/脚本能力（例如 43284 只有 Finish/LKM relay）。
 */
enum class BackendRunCapability {
    /** 有 Flutter 之外的 Flow run 入口，可由 `BackendRunDispatcher` 分发。 */
    FLOW_RUN,

    /** 没有 Flow 运行入口：运行页只能展示拒绝原因。 */
    NO_FLOW_RUN,
}

/**
 * 把一条原因渲染成**用户可见**的本地化文本（框架唯一实现）。
 *
 * @return 仅本地化文案；`detail` 只保留在数据层/报告，**不进入 UI 文本**。
 */
@Composable
fun ReadinessReason.displayText(): String = stringResource(messageRes)

/**
 * 单行本地化摘要：home 状态卡与运行页面板唯一使用的 readiness helper。
 *
 * 规则：
 * - Ready ⇒ 已有 ready 文案；
 * - NotReady ⇒ `模板 + 首条原因的本地化 messageRes`（不带 detail、不拼分号多原因）。
 *
 * @return 单行本地化文本；调用方负责 `maxLines=1`/省略号展示。
 */
@Composable
fun BackendReadiness.displayLine(): String = when (this) {
    is BackendReadiness.Ready -> stringResource(R.string.backend_readiness_ready)
    is BackendReadiness.NotReady -> {
        val firstRes = reasons.firstOrNull()?.messageRes
            ?: R.string.backend_readiness_undeclared
        stringResource(
            R.string.backend_readiness_not_ready_first_reason,
            stringResource(firstRes),
        )
    }
}

/**
 * 兼容旧调用点的别名：语义等价于 [displayLine]（单行本地化，不含 detail）。
 *
 * @return 同 [displayLine]。
 */
@Composable
fun BackendReadiness.displaySummary(): String = displayLine()
