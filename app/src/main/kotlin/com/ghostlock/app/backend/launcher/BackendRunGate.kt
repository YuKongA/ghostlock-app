package com.ghostlock.app.backend.launcher

import android.content.Context
import com.ghostlock.app.domain.privilege.AttackExecutorKind
import com.ghostlock.app.domain.privilege.AttackExecutorState
import com.ghostlock.app.backend.declarations.BackendReadiness
import com.ghostlock.app.backend.declarations.BackendRunCapability
import com.ghostlock.app.backend.declarations.ReadinessReason
import com.ghostlock.app.R
import com.ghostlock.app.domain.backend.BackendRegistryIdentity
import com.ghostlock.app.backend.declarations.BackendSpec
import kotlinx.coroutines.CancellationException

/**
 * 运行页上下文：不属于 backend 自身、但会改变 allow/deny 的页面事实。
 *
 * @property backendRunInProgress 是否已有一个 backend run 正在执行（UI 框架独占的事实）。
 * @property executor 当前攻击执行体（种类+授权二值；不读 uid）；用于比对 backend 声明。
 * @note 阶段：task-21；由运行页在每次渲染/点击时构造，不含 backend 内部状态。
 */
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
data class RunDecision(
    val allowed: Boolean,
    val reasons: List<ReadinessReason>,
)

/**
 * 应用运行门禁：**唯一** allow/deny 决策点。
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
        } else if (!spec.selectable) {
            reasons += ReadinessReason(R.string.backend_run_backend_disabled)
        }
        if (readiness is BackendReadiness.NotReady) {
            reasons += readiness.reasons
        }
        /* UI 只读 backend 的 uid 需求声明并拒绝；backend 不自检。 */
        if (spec != null) {
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
internal sealed interface BackendRunDispatch {
    /** 已交给 Flow 执行；state 是执行结果（Finished/Refused）。 */
    data class Executed(val state: BackendExecutionState) : BackendRunDispatch

    /** 框架拒绝执行：reasons 为 [RunDecision.reasons]。 */
    data class Blocked(val reasons: List<ReadinessReason>) : BackendRunDispatch
}

/**
 * backend 执行分发器：门禁通过后**直接**把 run 交给 Flow
 * （task-32 分层：不再经 Controller/Run 等中间类）。
 *
 * 链路：`BackendRunDispatcher.dispatch` → `FlowLaunchParent.launch` → 新进程 launcher
 * （APP/Shizuku）→ 该 backend 的 Flow。旧 in-process runner 通道已删除（B5c-1）。
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
     * B5c-1：**只调用 `FlowLaunchParent.launch`**（单一入口；无 backend 分支）。launcher 内部
     * 把异常映射为具名 Refused；本函数只兜底 launcher 抛出的非预期异常。
     *
     * @param spec 当前选中的 backend。
     * @param decision 门禁决策；allowed=false 时直接返回 [BackendRunDispatch.Blocked]。
     * @param context 应用上下文（输入组装用）。
     * @param executor 与门禁使用同一快照的执行体；分发器不得重新读取 UI 或偏好。
     * @param onLine 运行期事实行回调（终端展示；不参与控制流）。
     * @param registryIdentity app composition root 捕获的 registry 身份。
     * @return 分发结果；不抛给 UI（取消除外）。
     */
    suspend fun dispatch(
        spec: BackendSpec?,
        decision: RunDecision,
        context: Context,
        executor: AttackExecutorState,
        registryIdentity: BackendRegistryIdentity? = null,
        onLine: (String) -> Unit = {},
    ): BackendRunDispatch {
        if (!decision.allowed) {
            return BackendRunDispatch.Blocked(decision.reasons)
        }
        if (spec == null || spec.flowSymbol == null) {
            return BackendRunDispatch.Blocked(
                listOf(ReadinessReason(R.string.backend_run_no_flow_entry)),
            )
        }
        return try {
            /* B5c-1 唯一入口：dispatcher → FlowLaunchParent.launchSymbol → APP/Shizuku 新进程 launcher。 */
            val state = FlowLaunchParent.launch(
                context = context.applicationContext,
                executor = executor,
                spec = spec,
                registryIdentity = registryIdentity,
                onLine = onLine,
            )
            BackendRunDispatch.Executed(state)
        } catch (cancelled: CancellationException) {
            throw cancelled
        } catch (problem: Throwable) {
            BackendRunDispatch.Executed(
                BackendExecutionState.Refused(
                    listOf(
                        "backend-run-execution:exception:" + problem::class.java.simpleName,
                    ),
                ),
            )
        }
    }
}
