package com.ghostlock.app.ui.viewmodel

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.ViewModel
import com.ghostlock.app.R
import com.ghostlock.app.ui.backend.BackendReadiness
import com.ghostlock.app.ui.backend.BackendRunGate
import com.ghostlock.app.ui.backend.BackendRunUiState
import com.ghostlock.app.ui.backend.ReadinessReason
import com.ghostlock.app.ui.backend.RunDecision
import com.ghostlock.app.ui.backend.RunPageContext
import com.ghostlock.app.ui.backend.currentAttackExecutorState
import com.ghostlock.app.backend.declarations.BackendSelection
import com.ghostlock.app.backend.declarations.BackendSpec
import com.ghostlock.app.backend.registry.Backends

/**
 * 执行页（RunPage）的**纯 UI 状态 holder**（task-32 分层收敛）。
 *
 * 职责（只此而已）：
 * - 持有当前 43499 run 的 UI 状态（Idle/Running/Finished/Refused）；
 * - 给页面提供所选 backend 的 readiness/Gate 决策辅助（唯一决策点仍是 [BackendRunGate]）。
 *
 * **不再**持有 controller/执行器、不做请求组装、不做编排：门禁通过后页面经
 * `BackendRunDispatcher` **直接**调用 `Flow43499Impl().run(request)`。
 *
 * @note 每页一个实例（`viewModel()`），非全局单例；阶段：实验，未过真机门禁。
 */
class BackendRunViewModel : ViewModel() {
    /** 当前 run 的 UI 状态；页面写入 Running/结果，UI 只原样展示。 */
    internal var runState: BackendRunUiState by mutableStateOf(BackendRunUiState.Idle)

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
            RunPageContext(
                backendRunInProgress = runInProgress,
                executor = currentAttackExecutorState(),
            ),
        )
}
