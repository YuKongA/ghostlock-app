package com.ghostlock.app.backend.declarations

import com.ghostlock.app.BuildConfig
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
 * @note 不缓存、不持有可变全局：每个 app/host composition root 必须显式传入其一次性
 *   registry membership snapshot；registry 构造本身 fail-closed。
 * @note 阶段：G9（B4b 后收口）。
 */
object FlowEntryRegistryAssembly {
    /**
     * 生产符号：所有 `BackendSpec.flowSymbol`（去空/去重）。
     *
     * @return 稳定顺序的生产 Flow 符号列表；不含诊断 fixture。
     */
    fun productionSymbols(registryEntries: List<BackendSpec>): List<FlowSymbol> =
        registryEntries.asSequence()
            .mapNotNull { it.flowSymbol }
            .distinct()
            .toList()

    /**
     * 生产 registry（release/debug 相同）。
     *
     * @return [FlowEntryRegistry]；非法/重复符号抛具名异常（fail-closed）。
     */
    fun productionRegistry(registryEntries: List<BackendSpec>): FlowEntryRegistry =
        FlowEntryRegistry.of(productionSymbols(registryEntries))

    /**
     * host 实际使用的 allowlist：release=生产；debug=生产+诊断 fixture。
     *
     * @return [FlowEntryRegistry]；诊断 fixture 仅 debug 可解析。
     */
    fun hostRegistry(registryEntries: List<BackendSpec>): FlowEntryRegistry {
        if (!BuildConfig.DEBUG) return productionRegistry(registryEntries)
        return FlowEntryRegistry.of(
            productionSymbols(registryEntries) + DiagnosticFlowEntries.symbols,
        )
    }
}
