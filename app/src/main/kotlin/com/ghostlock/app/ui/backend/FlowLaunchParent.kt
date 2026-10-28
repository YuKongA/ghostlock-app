package com.ghostlock.app.ui.backend

import android.content.Context
import android.os.Bundle
import com.ghostlock.app.ui.screen.module.backends.Backends
import kotlinx.coroutines.asCoroutineDispatcher
import kotlinx.coroutines.withContext
import java.util.concurrent.Executors

/**
 * Flow 调用描述（跨执行体统一，**不传 lambda**）。
 *
 * @property symbol 稳定 Flow 符号：内置=backend key；动态 Lua=`lua:<scriptId>`。
 * @property config 可序列化配置（Bundle；由 backend/Lua 定义键）。
 * @note lambda 不能跨 AIDL；App/Shizuku 两侧都用同一 symbol 解析到同一 Flow。
 */
internal data class FlowSymbolInvocation(
    val symbol: String,
    val config: Bundle = Bundle(),
) {
    init {
        require(symbol.isNotBlank()) { "flow-symbol:blank" }
    }
}

/** 把 symbol 解析为「选定 Flow 并执行」的 seam；App/Shizuku 各自实现。 */
internal interface FlowSymbolResolver {
    /**
     * @param context 应用上下文（App 侧组装 Flow 输入用）。
     * @param invocation Flow 符号+配置。
     * @param onLine 进度行回调。
     * @return Flow 结果。
     */
    suspend fun resolve(
        context: Context?,
        invocation: FlowSymbolInvocation,
        onLine: (String) -> Unit,
    ): BackendRunUiState
}

/** App 侧解析器：symbol=backend key → `Backends` 注册表 → spec.runner(Flow)。 */
internal object BackendsFlowSymbolResolver : FlowSymbolResolver {
    override suspend fun resolve(
        context: Context?,
        invocation: FlowSymbolInvocation,
        onLine: (String) -> Unit,
    ): BackendRunUiState {
        val spec = Backends.byKey(invocation.symbol)
            ?: return BackendRunUiState.Refused(listOf("flow-symbol:unknown:" + invocation.symbol))
        val runner = spec.runner
            ?: return BackendRunUiState.Refused(listOf("flow-symbol:no-runner:" + invocation.symbol))
        val appContext = context
            ?: return BackendRunUiState.Refused(listOf("flow-symbol:context-missing"))
        return runner.run(appContext.applicationContext, onLine)
    }
}

/**
 * 攻击执行体（多态）：App/Shizuku 实现同一 [FlowLaunchExecutor]。
 *
 * @note UI 只选执行体；调用结构相同（context + symbol invocation + onLine），没有第二入口。
 */
internal interface FlowLaunchExecutor {
    /**
     * @param context 应用上下文。
     * @param invocation Flow 符号+配置。
     * @param onLine 进度行回调。
     * @return Flow 结果（或具名 Refused）。
     */
    suspend fun launch(
        context: Context?,
        invocation: FlowSymbolInvocation,
        onLine: (String) -> Unit,
    ): BackendRunUiState
}

/**
 * App 执行体：新起专用 JVM 线程，按 symbol 解析并在 App JVM 内执行选定 Flow。
 *
 * @param resolver symbol→Flow 解析器（生产=[BackendsFlowSymbolResolver]；测试可注入）。
 */
internal class LocalThreadFlowExecutor(
    private val resolver: FlowSymbolResolver = BackendsFlowSymbolResolver,
) : FlowLaunchExecutor {
    override suspend fun launch(
        context: Context?,
        invocation: FlowSymbolInvocation,
        onLine: (String) -> Unit,
    ): BackendRunUiState {
        val dispatcher = Executors.newSingleThreadExecutor { runnable ->
            Thread(runnable, "ghostlock-attack").apply { isDaemon = true }
        }.asCoroutineDispatcher()
        return try {
            withContext(dispatcher) { resolver.resolve(context, invocation, onLine) }
        } finally {
            runCatching { dispatcher.close() }
        }
    }
}

/**
 * Shizuku 执行体：把同一 symbol+config 交给 Shizuku host JVM，由 host 侧解析并执行同一 Flow。
 *
 * @note host Flow 执行（ADR-0022 变更）属 R2；当前返回具名 pending，不静默回退 App。
 */
internal class ShizukuHostFlowExecutor : FlowLaunchExecutor {
    override suspend fun launch(
        context: Context?,
        invocation: FlowSymbolInvocation,
        onLine: (String) -> Unit,
    ): BackendRunUiState = BackendRunUiState.Refused(
        listOf("flow-launcher:shizuku-remote-pending:" + invocation.symbol),
    )
}

/**
 * Flow 上一级（多态分发）：**唯一**决定用哪个攻击执行体跑同一 symbol Flow 的地方。
 */
internal object FlowLaunchParent {
    /**
     * @param executor 当前攻击执行体状态（UI 选择 + 授权二值）。
     * @return 对应 [FlowLaunchExecutor] 实现。
     */
    internal fun executorFor(executor: AttackExecutorState): FlowLaunchExecutor =
        when (executor.kind) {
            AttackExecutorKind.APP -> LocalThreadFlowExecutor()
            AttackExecutorKind.SHIZUKU -> ShizukuHostFlowExecutor()
        }

    /**
     * 统一调用结构：两种执行体同一签名、同一 symbol。
     *
     * @param context 应用上下文。
     * @param executor 当前攻击执行体状态。
     * @param invocation Flow 符号+配置。
     * @param onLine 进度行回调。
     * @return Flow 结果。
     */
    suspend fun launch(
        context: Context,
        executor: AttackExecutorState,
        invocation: FlowSymbolInvocation,
        onLine: (String) -> Unit,
    ): BackendRunUiState = executorFor(executor).launch(context, invocation, onLine)
}
