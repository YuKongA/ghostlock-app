package com.ghostlock.app.backend.declarations

import com.ghostlock.app.BuildConfig
import com.ghostlock.app.backend.registry.Backends
import com.ghostlock.app.backend.diagnostics.DiagnosticFlowEntries
import com.ghostlock.app.core.flow.FlowEntryRegistry
import com.ghostlock.app.core.flow.FlowSymbol

/**
 * 由 `BackendSpec` 生成 Flow 入口 registry 的唯一组装点（G9）。
 *
 * 计划 §2.4：registry=唯一 allowlist，必须由 `BackendSpec` 生成（Lua/UI 不得自带类名）。
 * 两个 host（app `:flowhost` / Shizuku `GhostlockUserService`）只消费本组装结果；
 * **debug** 构建额外追加无副作用诊断 fixture，**release** 不含诊断符号。
 *
 * @note 不缓存、不持有可变全局：每次调用重新从 [Backends.all] 取 spec 并构造 registry；
 *   registry 构造本身 fail-closed（非法/重复符号抛具名异常，调用方保持 `flow-symbol:*` 失败）。
 * @note 动态 backend（Lua）若进入 [Backends.all] 且声明 `flowSymbol` 会自动纳入；未声明者不会
 *   被 host 接受（B5 Lua 迁移需提供注册路径）。
 * @note 阶段：G9（B4b 后收口）。
 */
object FlowEntryRegistryAssembly {
    /**
     * 生产符号：所有 `BackendSpec.flowSymbol`（去空/去重）。
     *
     * @return 稳定顺序的生产 Flow 符号列表；不含诊断 fixture。
     */
    fun productionSymbols(): List<FlowSymbol> =
        Backends.all.asSequence()
            .mapNotNull { it.flowSymbol }
            .distinct()
            .toList()

    /**
     * 生产 registry（release/debug 相同）。
     *
     * @return [FlowEntryRegistry]；非法/重复符号抛具名异常（fail-closed）。
     */
    fun productionRegistry(): FlowEntryRegistry = FlowEntryRegistry.of(productionSymbols())

    /**
     * host 实际使用的 allowlist：release=生产；debug=生产+诊断 fixture。
     *
     * @return [FlowEntryRegistry]；诊断 fixture 仅 debug 可解析。
     */
    fun hostRegistry(): FlowEntryRegistry {
        if (!BuildConfig.DEBUG) return productionRegistry()
        return FlowEntryRegistry.of(productionSymbols() + DiagnosticFlowEntries.symbols)
    }
}
