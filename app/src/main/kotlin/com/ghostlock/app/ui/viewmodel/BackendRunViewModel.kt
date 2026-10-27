package com.ghostlock.app.ui.viewmodel

import android.content.Context
import androidx.lifecycle.ViewModel
import com.ghostlock.app.R
import com.ghostlock.app.ui.backend.BackendReadiness
import com.ghostlock.app.ui.backend.BackendRunDispatch
import com.ghostlock.app.ui.backend.BackendRunDispatcher
import com.ghostlock.app.ui.backend.BackendRunGate
import com.ghostlock.app.ui.backend.ReadinessReason
import com.ghostlock.app.ui.backend.RunDecision
import com.ghostlock.app.ui.backend.RunPageContext
import com.ghostlock.app.ui.screen.module.backends.BackendSelection
import com.ghostlock.app.ui.screen.module.backends.BackendSpec
import com.ghostlock.app.ui.screen.module.backends.Backends
import com.ghostlock.app.ui.screen.module.backends.Cve43499RunController

/**
 * 执行页（RunPage）持有的 backend 运行执行器与 Gate 决策辅助。
 *
 * 架构约束（task-25）：
 * - **唯一 allow/deny 决策点仍是 `BackendRunGate`**，本 VM 只把所选 backend 的 readiness
 *   声明与运行页上下文喂给 Gate，并把结果交给页面展示/按钮禁用；
 * - 点击 Start 后由 `BackendRunDispatcher` 再判一次并分发（43499 → `Cve43499RunController`）；
 * - 不修改 backend readiness 声明/文案语义，不做 retry/fallback。
 *
 * @note 每页一个实例（`viewModel()`），非全局单例；阶段：实验，未过真机门禁。
 */
class BackendRunViewModel : ViewModel() {
    /** 43499 执行器；被本 VM 持有（页面只读取其状态与触发 run）。 */
    internal val controller: Cve43499RunController = Cve43499RunController()

    /** @return 当前选中的 backend；未选择时 null。 */
    internal fun selectedSpec(): BackendSpec? = Backends.all.getOrNull(BackendSelection.index)

    /** @return 当前所选 backend 的 readiness 声明（真实来源，缺省 fail-closed）。 */
    internal fun readiness(): BackendReadiness =
        selectedSpec()?.readiness() ?: BackendReadiness.NotReady(
            listOf(ReadinessReason(R.string.backend_run_no_backend)),
        )

    /**
     * 计算一次 Gate 决策（页面用于按钮 enabled 与原因展示）。
     *
     * @param runInProgress 是否已有 backend run 在执行中（运行页上下文）。
     * @return 唯一决策点给出的 [RunDecision]。
     */
    internal fun decide(runInProgress: Boolean): RunDecision =
        BackendRunGate.decide(
            selectedSpec(),
            readiness(),
            RunPageContext(backendRunInProgress = runInProgress),
        )

    /**
     * Start 点击路径：经 Gate 再判一次后由框架分发执行。
     *
     * @param context 应用上下文。
     * @param runInProgress 运行页当前是否处于执行中。
     * @return 分发结果（Blocked 带原因 / Executed 带执行器状态）。
     * @note 调用方负责把结果写进 terminal 与状态机；本方法不自行重试。
     */
    internal suspend fun runOnce(
        context: Context,
        runInProgress: Boolean,
    ): BackendRunDispatch = BackendRunDispatcher.dispatch(
        selectedSpec(),
        decide(runInProgress),
        controller,
        context.applicationContext,
    )
}
