# 脚本调用下落为 op 的三条规则

> 分析类（≤8 KiB）· 权威来源：**design-critic 裁定**（2026-10-08，R3 确认）+ `docs/analysis/handoff-script-layer-p5-spec.md` §3；
> 本件只把该裁定落成可检索的条文，**不新增决定** ✓。

脚本的调用经 staged 计划**下落**为**计划侧**的 op 条目（`pipeline::plan_gate` 消费 ✓）。三条规则缺一不可：

1. **窗口 op 由链/宿主选定，脚本不参与** ✓ —— op 记号来自**链的组合**；`kOpStepSetBindings`（`contract/chain_catalog.hpp` ✓）在计划侧把 op 与 step set 绑定，并有编译期断言（可用 op 恰一个绑定、step set 不重复绑定 ✓）。脚本**没有**、也**不得有**能拼出 `window.*` 的入口：冻结判据 = `script_surface.hpp` 的 `surface_has_no_window_prefixed_names()` 断言 + `make -C core check` 的 grep ✓。
2. **脚本的 `writeop`/`copyop`/`settle` 是窗口相位写原语的数据** ✓ —— 记入 staged 计划的 **surface 条目**（`script/staged_plan.hpp` ✓），由实现层**在窗口相位**执行 ⇒ 「**窗口内零 Lua**」由此保持（`script/sandbox.hpp` 的 stage-only 入口是结构性保证 ✓）。
3. **参数来源** ✓ —— 脚本可给**写原语自己的**参数（`target` / `value` / `width` ✓，取值来自只读 `cfg` 快照的 profile 键路径 ✓）；**不可给 op 参数**（`attempts` / `nice` / `settle_us` / `burst_calls` 等 ✓）—— 那些由 profile 与机制层给，`ParamSpec.max` 是**唯一上界** ✓。否则脚本就成了 op 参数的来源，与规则 1 相抵 ✗。

**判据（可执行）**：`make -C core check` EXIT=0 ✓（含 `window.*` 前缀 grep 与两处绑定断言）；`chain_catalog_test` 覆盖 op↔step set 绑定 ✓；`plan_load_test` 覆盖「脚本调用 → staged 记录」的端到端 ✓。
