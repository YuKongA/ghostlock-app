package com.ghostlock.app.ui.backend

import android.content.Context
import androidx.compose.runtime.Composable
import androidx.compose.runtime.Immutable
import androidx.compose.ui.res.stringResource
import com.ghostlock.app.R
import com.ghostlock.app.core.flow.FlowResult
import com.ghostlock.app.backend.declarations.BackendSpec
import com.ghostlock.app.backend.declarations.Backends
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/**
 * 运行页上下文：不属于 backend 自身、但会改变 allow/deny 的页面事实。
 *
 * @property backendRunInProgress 是否已有一个 backend run 正在执行（UI 框架独占的事实）。
 * @property executor 当前攻击执行体（种类+授权二值；不读 uid）；用于比对 backend 声明。
 * @note 阶段：task-21；由运行页在每次渲染/点击时构造，不含 backend 内部状态。
 */
@Immutable
data class RunPageContext(
    val backendRunInProgress: Boolean = false,
    val executor: AttackExecutorState = AttackExecutorState(AttackExecutorKind.APP, true),
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
 * 2. backend disabled ⇒ deny `backend_run_backend_disabled`；
 * 3. readiness 为 NotReady ⇒ deny 并附全部声明原因；
 * 4. backend 无 Flow 运行入口（如 43284 只有 Finish/LKM relay）⇒ deny `backend_run_no_flow_entry`；
 * 5. 已有 backend run 在执行 ⇒ deny `backend_run_in_progress`；
 * 6. 以上皆无 ⇒ allow。
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
        } else if (!Backends.isSelectable(spec)) {
            reasons += ReadinessReason(R.string.backend_run_backend_disabled)
        }
        if (readiness is BackendReadiness.NotReady) {
            reasons += readiness.reasons
        }
        /* UI 只读 backend 的 uid 需求声明并拒绝；backend 不自检。 */
        if (spec != null && !spec.appOwnedFlowWithAutomaticProcessLauncher) {
            if (context.executor.kind == AttackExecutorKind.SHIZUKU && !context.executor.authorized) {
                reasons += ReadinessReason(R.string.backend_run_executor_unavailable)
            } else if (!spec.uidRequirement.satisfiedBy(context.executor.kind)) {
                reasons += ReadinessReason(R.string.backend_run_uid_requirement)
            }
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
 * 框架分发结果：要么已执行（带最终状态），要么被门禁拒绝（带原因）。
 *
 * @note 本类型只服务运行页展示；不构成 supported/真机结论。
 */
@Immutable
internal sealed interface BackendRunDispatch {
    /** 已交给 Flow 执行；state 是执行结果（Finished/Refused）。 */
    data class Executed(val state: BackendRunUiState) : BackendRunDispatch

    /** 框架拒绝执行：reasons 为 [RunDecision.reasons]。 */
    data class Blocked(val reasons: List<ReadinessReason>) : BackendRunDispatch
}

/**
 * UI 框架的 backend 执行分发器：门禁通过后**直接**把 run 交给 Flow
 * （task-32 分层：不再经 Controller/Run 等中间类）。
 *
 * 链路：`BackendRunDispatcher.dispatch` → 所选 spec 的 runner → `FlowLaunchParent`
 * → 该 backend 的 Flow。选择和编排由各 backend 声明及其 Flow 负责。
 *
 * exec-glue build 中只有 `43499-Glued` 与 `43284Glued` 可选择并具备 `FLOW_RUN` 入口；
 * 原版 backend 与 Lua 由注册表 allowlist 拒绝（防御性双保险）。
 *
 * @note 决策由 [BackendRunGate] 给出；本对象不重新判断“是否应该允许”，只路由到 Flow。
 * @note 阶段：task-32；不接默认 Flow/UI 入口，页面必须显式调用。
 */
internal object BackendRunDispatcher {
    /**
     * 执行一次已获门禁允许的 backend run。
     *
     * R1a：**只调用 `spec.runner`**（单一注册点；无 backend 分支）。runner 内部已把异常映射为
     * 具名 Refused；本函数只兜底 runner 抛出的非预期异常。
     *
     * @param spec 当前选中的 backend。
     * @param decision 门禁决策；allowed=false 时直接返回 [BackendRunDispatch.Blocked]。
     * @param context 应用上下文（输入组装用）。
     * @param onLine 运行期事实行回调（终端展示；不参与控制流）。
     * @return 分发结果；不抛给 UI（取消除外）。
     */
    suspend fun dispatch(
        spec: BackendSpec?,
        decision: RunDecision,
        context: Context,
        onLine: (String) -> Unit = {},
    ): BackendRunDispatch {
        if (!decision.allowed) {
            return BackendRunDispatch.Blocked(decision.reasons)
        }
        if (spec == null || spec.runner == null) {
            return BackendRunDispatch.Blocked(
                listOf(ReadinessReason(R.string.backend_run_no_flow_entry)),
            )
        }
        return try {
            /* 唯一入口：UI → dispatcher → FlowLaunchParent（多态执行体）→ symbol 解析 → Flow。 */
            val invocation = FlowSymbolInvocation(symbol = spec.key)
            val selectedExecutor = if (spec.appOwnedFlowWithAutomaticProcessLauncher) {
                AttackExecutorState(AttackExecutorKind.APP, authorized = true)
            } else {
                currentAttackExecutorState()
            }
            val state = FlowLaunchParent.launch(
                context = context.applicationContext,
                executor = selectedExecutor,
                invocation = invocation,
                onLine = onLine,
            )
            BackendRunDispatch.Executed(state)
        } catch (cancelled: CancellationException) {
            throw cancelled
        } catch (problem: Throwable) {
            BackendRunDispatch.Executed(
                BackendRunUiState.Refused(
                    listOf("backend-run-execution:exception:" + problem::class.java.simpleName),
                ),
            )
        }
    }
}

/**
 * 运行页展示所需的一份只读面板数据（由 UI 框架计算，页面只负责渲染）。
 *
 * @property cve 当前选中 backend 的 CVE 身份。
 * @property readiness 该 backend 的 readiness 声明（真实来源）。
 * @property decision [BackendRunGate] 的唯一 allow/deny 结论。
 * @property runState 43499 运行的最新状态（Idle/Running/Finished/Refused）。
 * @note 阶段：task-21/32；不含任何执行能力，页面不能绕过 [BackendRunGate]。
 */
@Immutable
data class BackendRunPanel(
    val cve: String,
    val readiness: BackendReadiness,
    val decision: RunDecision,
    val runState: BackendRunUiState,
)

/**
 * 把运行状态渲染成展示行（框架唯一实现）。
 *
 * @return 稳定顺序的文本行；Idle 为空。
 * @note 只展示事实，不做二次判定。
 */
@Composable
fun BackendRunUiState.displayLines(): List<String> = when (this) {
    BackendRunUiState.Idle -> emptyList()
    BackendRunUiState.Running -> listOf(stringResource(R.string.backend_run_in_progress))
    is BackendRunUiState.Refused ->
        listOf(stringResource(R.string.backend_run_denied)) + reasons
    is BackendRunUiState.Finished -> when (val result = result) {
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
