package com.ghostlock.app.ui.backend

import android.content.Context
import com.ghostlock.app.backend.launcher.AppProcessFlowLauncher
import com.ghostlock.app.backend.host.FlowEntryParams
import com.ghostlock.app.backend.launcher.ShizukuProcessFlowLauncher
import com.ghostlock.app.backend.declarations.BackendSpec
import com.ghostlock.app.core.flow.FlowSymbol

/**
 * Flow 上一级：**唯一**把已迁移 backend 的 Flow 交给新进程通道的地方（B5c-1）。
 *
 * 旧 in-process runner 与 App/Shizuku 双执行体通道（`FlowSymbolInvocation`/
 * `FlowSymbolResolver`/`FlowLaunchExecutor`/`LocalThreadFlowExecutor`/
 * `ShizukuHostFlowExecutor`）已删除；攻击一律经 APP/Shizuku 新进程 launcher。
 *
 * @note [`launch`] 是唯一调用入口，[`launchSymbol`] 为其实施；两者语义相同。
 * @note 阶段：B5c-1（迁移期收口）。
 */
internal object FlowLaunchParent {
    /**
     * 唯一入口：按 executor 选择 APP/Shizuku 新进程通道执行已迁移 Flow。
     *
     * @param context app context（UI 进程参数构造）。
     * @param executor 当前攻击执行体（APP/Shizuku + 授权二值）。
     * @param spec 已迁移 backend（`flowSymbol` 必需）。
     * @param onLine 运行期事实行回调。
     * @return launcher 的 BackendRunUiState（Finished/HandoffRequested/Refused）。
     */
    suspend fun launch(
        context: Context,
        executor: AttackExecutorState,
        spec: BackendSpec,
        onLine: (String) -> Unit = {},
    ): BackendRunUiState = launchSymbol(context, executor, spec, onLine)

    /**
     * 把已迁移 backend 的 Flow 符号 + 参数交给新进程通道（APP/Shizuku launcher）。
     *
     * 顺序：spec.flowSymbol 必需 → `spec.flowParams(context)`（Refused 原样透传）→
     * 调用方参数拒绝 `host.*` → 按 executor 选 `AppProcessFlowLauncher` 或
     * `ShizukuProcessFlowLauncher`；不 fallback。
     *
     * @param context app context（UI 进程参数构造）。
     * @param executor 当前攻击执行体（APP/Shizuku + 授权二值）。
     * @param spec 已迁移 backend。
     * @param onLine 运行期事实行回调。
     * @return launcher 的 BackendRunUiState（Finished/HandoffRequested/Refused）。
     */
    suspend fun launchSymbol(
        context: Context,
        executor: AttackExecutorState,
        spec: BackendSpec,
        onLine: (String) -> Unit = {},
    ): BackendRunUiState {
        val flowSymbol: FlowSymbol = spec.flowSymbol
            ?: return BackendRunUiState.Refused(listOf("flow-launcher:symbol:missing:" + spec.key))
        val params = when (val built = spec.flowParams(context.applicationContext)) {
            is BackendFlowParams.Ready -> built.params
            is BackendFlowParams.Refused -> return BackendRunUiState.Refused(built.reasons)
        }
        try {
            FlowEntryParams.validateUserParams(params)
        } catch (problem: IllegalArgumentException) {
            return BackendRunUiState.Refused(
                listOf(problem.message ?: "flow-params:reserved-key:unknown"),
            )
        }
        val appContext = context.applicationContext
        return when (executor.kind) {
            AttackExecutorKind.APP -> AppProcessFlowLauncher(appContext)
                .run(flowSymbol.canonical, params, null, onLine)

            AttackExecutorKind.SHIZUKU -> if (!executor.authorized) {
                BackendRunUiState.Refused(
                    listOf("flow-launcher:shizuku:preflight:not-granted"),
                )
            } else {
                ShizukuProcessFlowLauncher(appContext)
                    .run(flowSymbol.canonical, params, null, onLine)
            }
        }
    }
}
