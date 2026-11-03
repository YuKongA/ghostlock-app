package com.ghostlock.app.ui.backend

/**
 * backend 为新进程 Flow 通道构造的调用参数（B4a）。
 *
 * @note `Ready.params` 会在 UI 进程先剔除/拒绝 `host.*`；host 进程再注入自己的运行时事实。
 * @note 阶段：B4a（迁移期；B5c 收口）。
 */
sealed interface BackendFlowParams {
    /** 构造成功：相对稳定的业务参数（不含 host.*）。 */
    data class Ready(val params: Map<String, Any?>) : BackendFlowParams

    /** 构造失败：具名原因（UI 原样展示，不 fallback 旧通道）。 */
    data class Refused(val reasons: List<String>) : BackendFlowParams
}
