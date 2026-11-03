package com.ghostlock.app.core.flow

/**
 * Flow 入口 registry：**唯一 allowlist**（计划 §2.4）。
 *
 * 只有登记过的 symbol 才能被 host/launcher 解析；Lua/UI 不得自带类名。
 * 构造时对每个候选做完整反射校验；非法或重复一律 `error()` 具名失败（fail-closed）。
 *
 * @note 不可变：构造后不增删；无新增可变全局。
 * @note 阶段：B1 契约层；registry 与 BackendSpec 双向一致属 B2+。
 */
class FlowEntryRegistry private constructor(
    private val entries: Map<FlowSymbol, FlowEntrySpec>,
) {
    /** @return 已登记符号集合（只读）。 */
    val symbols: Set<FlowSymbol>
        get() = entries.keys

    /**
     * 解析一个符号。
     *
     * @param symbol 待解析符号。
     * @return 已登记时 [FlowEntryResolution.Resolved]；否则具名 Rejected
     *   （`flow-symbol:unregistered`）。
     */
    fun resolve(symbol: FlowSymbol): FlowEntryResolution =
        entries[symbol]?.let { FlowEntryResolution.Resolved(it) }
            ?: FlowEntryResolution.Rejected("flow-symbol:unregistered")

    companion object {
        /**
         * 从候选符号构造 registry。
         *
         * @param candidates 候选符号（可重复/非法；本函数 fail-closed）。
         * @return 不可变 registry。
         * @throws IllegalStateException 非法候选：
         *   `flow-registry:invalid:<reason>:<canonical>`；重复符号：
         *   `flow-registry:duplicate-symbol:<canonical>`。
         * @note 顺序：逐个 validate，任何 non-Resolved 立即失败（不部分登记）。
         */
        fun of(candidates: Iterable<FlowSymbol>): FlowEntryRegistry {
            val entries = LinkedHashMap<FlowSymbol, FlowEntrySpec>()
            for (symbol in candidates) {
                val resolution = FlowEntryValidator.validate(symbol)
                val spec = when (resolution) {
                    is FlowEntryResolution.Resolved -> resolution.entry
                    is FlowEntryResolution.Rejected -> error(
                        "flow-registry:invalid:" + resolution.reason + ":" + symbol.canonical,
                    )
                }
                if (entries.put(symbol, spec) != null) {
                    error("flow-registry:duplicate-symbol:" + symbol.canonical)
                }
            }
            return FlowEntryRegistry(entries.toMap())
        }
    }
}
