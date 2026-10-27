package com.ghostlock.app.ui.backend

import android.content.Context
import androidx.compose.runtime.Composable
import androidx.compose.runtime.Immutable
import androidx.compose.ui.res.stringResource
import com.ghostlock.app.R
import com.ghostlock.app.core.flow.FlowResult
import com.ghostlock.app.ui.screen.module.backends.BackendSpec
import com.ghostlock.app.ui.screen.module.backends.Cve43499RunController
import com.ghostlock.app.ui.screen.module.backends.Cve43499RunUiState

/**
 * 运行页上下文：不属于 backend 自身、但会改变 allow/deny 的页面事实。
 *
 * @property backendRunInProgress 是否已有一个 backend run 正在执行（UI 框架独占的事实）。
 * @note 阶段：task-21；由运行页在每次渲染/点击时构造，不含 backend 内部状态。
 */
@Immutable
data class RunPageContext(
    val backendRunInProgress: Boolean = false,
)

/**
 * 运行门禁决策：**唯一** allow/deny 结论 + 具名原因。
 *
 * @property allowed 是否允许执行 backend run。
 * @property reasons allowed=false 时的具名原因（UI 原样展示，不得吞掉）。
 */
@Immutable
data class RunDecision(
    val allowed: Boolean,
    val reasons: List<ReadinessReason>,
)

/**
 * UI 框架的运行门禁：**唯一** allow/deny 决策点。
 *
 * 决策输入 = 所选 backend 的 readiness 声明（真实来源，见各 `BackendSpec` 实现）
 * + 运行页上下文；输出 [RunDecision]。backend/Flow/Controller 不得再自行决定
 * “是否允许运行”（执行期失败仍以具名结果返回，由页面展示）。
 *
 * 规则（顺序固定，原因全部保留）：
 * 1. 未选择 backend ⇒ deny `backend_run_no_backend`；
 * 2. readiness 为 NotReady ⇒ deny 并附全部声明原因；
 * 3. backend 无 Flow 运行入口（如 43284 只有 Finish/LKM relay）⇒ deny `backend_run_no_flow_entry`；
 * 4. 已有 backend run 在执行 ⇒ deny `backend_run_in_progress`；
 * 5. 以上皆无 ⇒ allow。
 *
 * @note 本对象不执行任何 I/O；Lua 脚本运行仍由 `ScriptRunUiState.canRun` 决定，不经过本门禁。
 * @note 阶段：task-21（实验态；未过真机）。
 */
object BackendRunGate {
    /**
     * 计算一次运行门禁决策。
     *
     * @param spec 当前选中的 backend；null 表示没有选择。
     * @param readiness 该 backend 的 readiness 声明（真实来源）。
     * @param context 运行页上下文。
     * @return 不可变决策；allowed=true 时 reasons 为空。
     */
    fun decide(
        spec: BackendSpec?,
        readiness: BackendReadiness,
        context: RunPageContext = RunPageContext(),
    ): RunDecision {
        val reasons = mutableListOf<ReadinessReason>()
        if (spec == null) {
            reasons += ReadinessReason(R.string.backend_run_no_backend)
        }
        if (readiness is BackendReadiness.NotReady) {
            reasons += readiness.reasons
        }
        if (spec != null && spec.runCapability != BackendRunCapability.FLOW_RUN) {
            reasons += ReadinessReason(R.string.backend_run_no_flow_entry)
        }
        if (context.backendRunInProgress) {
            reasons += ReadinessReason(R.string.backend_run_in_progress)
        }
        return RunDecision(allowed = reasons.isEmpty(), reasons = reasons)
    }
}

/**
 * 框架分发结果：要么已执行（带 controller 状态），要么被门禁拒绝（带原因）。
 *
 * @note 本类型只服务运行页展示；不构成 supported/真机结论。
 */
@Immutable
internal sealed interface BackendRunDispatch {
    /** 已交给对应 backend 执行器；state 是执行器的最新状态。 */
    data class Executed(val state: Cve43499RunUiState) : BackendRunDispatch

    /** 框架拒绝执行：reasons 为 [RunDecision.reasons]。 */
    data class Blocked(val reasons: List<ReadinessReason>) : BackendRunDispatch
}

/**
 * UI 框架的 backend 执行分发器：门禁通过后把 run 交给对应 backend 执行器。
 *
 * 当前只有 CVE-2026-43499 具备 `FLOW_RUN` 入口（`Cve43499RunController`）；其它 backend
 * 即使声明允许也会以 `backend_run_no_flow_entry` 拒绝（防御性双保险）。
 *
 * @note 决策由 [BackendRunGate] 给出；本对象不重新判断“是否应该允许”，只路由到执行器。
 * @note 阶段：task-21；不接默认 Flow/UI 入口，页面必须显式调用。
 */
internal object BackendRunDispatcher {
    /** 具备 Flow run 入口的 backend 身份（与 `BackendSpec.cve` 对齐）。 */
    private const val CVE_43499: String = "CVE-2026-43499"

    /**
     * 执行一次已获门禁允许的 backend run。
     *
     * @param spec 当前选中的 backend。
     * @param decision 门禁决策；allowed=false 时直接返回 [BackendRunDispatch.Blocked]。
     * @param controller 43499 的执行器（由调用页面持有，不在本对象内保存状态）。
     * @param context 应用上下文。
     * @return 分发结果。
     * @note 本函数不抛异常：执行器内部的异常由 controller 映射成具名状态。
     */
    suspend fun dispatch(
        spec: BackendSpec?,
        decision: RunDecision,
        controller: Cve43499RunController,
        context: Context,
    ): BackendRunDispatch {
        if (!decision.allowed) {
            return BackendRunDispatch.Blocked(decision.reasons)
        }
        if (spec == null || spec.cve != CVE_43499) {
            return BackendRunDispatch.Blocked(
                listOf(ReadinessReason(R.string.backend_run_no_flow_entry)),
            )
        }
        controller.run(context.applicationContext)
        return BackendRunDispatch.Executed(controller.state)
    }
}

/**
 * 运行页展示所需的一份只读面板数据（由 UI 框架计算，页面只负责渲染）。
 *
 * @property cve 当前选中 backend 的 CVE 身份。
 * @property readiness 该 backend 的 readiness 声明（真实来源）。
 * @property decision [BackendRunGate] 的唯一 allow/deny 结论。
 * @property runState 43499 执行器的最新状态（Idle/Running/Finished/Refused）。
 * @note 阶段：task-21；不含任何执行能力，页面不能绕过 [BackendRunGate]。
 */
@Immutable
data class BackendRunPanel(
    val cve: String,
    val readiness: BackendReadiness,
    val decision: RunDecision,
    val runState: Cve43499RunUiState,
)

/**
 * 把执行器状态渲染成展示行（框架唯一实现）。
 *
 * @return 稳定顺序的文本行；Idle 为空。
 * @note 只展示事实，不做二次判定。
 */
@Composable
fun Cve43499RunUiState.displayLines(): List<String> = when (this) {
    Cve43499RunUiState.Idle -> emptyList()
    Cve43499RunUiState.Running -> listOf(stringResource(R.string.backend_run_in_progress))
    is Cve43499RunUiState.Refused ->
        listOf(stringResource(R.string.backend_run_denied)) + reasons
    is Cve43499RunUiState.Finished -> when (val result = result) {
        is FlowResult.Ok ->
            listOf("OK") + result.evidence.map { (key, value) -> key + " = " + value }
        is FlowResult.Failed ->
            listOf(result.reason) + result.evidence.map { (key, value) -> key + " = " + value }
    }
}

/**
 * 门禁决策的单行本地化摘要（运行页面板使用；不带 detail、不拼多原因）。
 *
 * @return allowed ⇒ 允许文案；deny ⇒ `模板 + 首条原因的本地化 messageRes`。
 */
@Composable
fun RunDecision.displayLine(): String = if (allowed) {
    stringResource(R.string.backend_run_allowed)
} else {
    val firstRes = reasons.firstOrNull()?.messageRes
        ?: R.string.backend_run_no_flow_entry
    stringResource(
        R.string.backend_run_denied_first_reason,
        stringResource(firstRes),
    )
}
