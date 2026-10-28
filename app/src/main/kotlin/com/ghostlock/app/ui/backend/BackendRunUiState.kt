package com.ghostlock.app.ui.backend

import androidx.compose.runtime.Immutable
import com.ghostlock.app.core.flow.FlowResult

/**
 * backend 运行页的中性可观察状态（B3；从 43499 专用类型迁移）。
 *
 * 状态只属于页面实例（ViewModel `remember` 持有），不是全局单例；`Finished` 只承载
 * Flow 结果、`Refused` 只承载具名原因，UI **不做二次判定**（ADR-0022）。
 *
 * @note 「不确定但必须重启」不以独立态表达：它是 `Finished(Failed)` 的 evidence
 *   （`mustReboot=true`）+ 独立重启警示条（见计划 delta §E）；`must_reboot` 唯一权威
 *   仍是 `data/cve98260/BootSafetyStore`。
 */
@Immutable
sealed interface BackendRunUiState {
    /** 尚未运行（或页面刚创建）。 */
    data object Idle : BackendRunUiState

    /** 正在执行：按钮应禁用，UI 显示运行中。 */
    data object Running : BackendRunUiState

    /** Flow 返回了结果（Ok/Failed）；UI 只原样展示，不做二次判定。 */
    data class Finished(val result: FlowResult) : BackendRunUiState

    /** 执行前/执行中被具名拒绝；reasons 稳定可检索。 */
    data class Refused(val reasons: List<String>) : BackendRunUiState
}
