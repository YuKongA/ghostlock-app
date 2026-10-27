package com.ghostlock.app.ui.viewmodel

import android.app.Application
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.AndroidViewModel
import com.ghostlock.app.ui.backend.BackendSessionStore
import com.ghostlock.app.ui.screen.module.backends.BackendSelection

/**
 * 「路径」页（backend 选择 + CVE-2026-43499 实验链）的页面状态。
 *
 * 背景（task-20）：旧 `ModuleViewModel` 是已移除 KernelSU 模块页的遗留（全仓无引用），
 * 与路径页无关；路径页此前把选择状态放在 `remember` 里、把运行 controller 放在
 * Composable 里，命名与归属都不清晰。本类把页面所需状态收敛到路径语义：
 * - 当前 backend 选择（与 [BackendSelection] 同步，供底部导航/主页 badge 共用）。
 *
 * 运行职责不在本 VM（task-21 架构）：路径页只配置；是否需要/允许运行由运行页的
 * `BackendRunGate` 决定，执行由 `BackendRunDispatcher` 分发。
 *
 * 选择持久化（task-27）：`selectBackend` 同时经 UI 框架 `BackendSessionStore` 保存
 * `selected_cve` 并让该 backend 保存/恢复自己的配置内容；启动恢复在 MainActivity。
 *
 * @param application 应用上下文（持久化用；Compose `viewModel()` 默认工厂提供）。
 * @note 每个路径页 Composition 通过 `viewModel()` 持有一个实例；不是全局单例。
 * @note 阶段：实验（CVE-2026-43499 未过真机门禁，不得作为 supported 宣称）。
 */
class PathViewModel(application: Application) : AndroidViewModel(application) {
    /** 当前选中的 backend 下标；读取即触发 Compose 重组。 */
    var selectedIndex: Int by mutableIntStateOf(BackendSelection.index)
        private set

    /**
     * 切换选中的 backend。
     *
     * @param index [com.ghostlock.app.ui.screen.module.backends.Backends.all] 的下标。
     * @note 同步写回 [BackendSelection]，保持主页 badge/底部导航与路径页一致。
     */
    fun selectBackend(index: Int) {
        selectedIndex = index
        BackendSelection.index = index
        /* task-27：保存选择并同步该 backend 的配置内容（框架负责，内容由 backend 提供）。 */
        runCatching {
            val app = getApplication<Application>()
            BackendSessionStore.from(app).select(app, index)
        }
    }
}
