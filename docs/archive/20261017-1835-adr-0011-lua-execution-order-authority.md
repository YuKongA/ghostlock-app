> 归档说明（2026-10-17）：归档原因 = **ADR 废除**（Lua / script / JCS 载体已删除，见 docs/plan/MASTER-PLAN.md §0.1）。原始路径 = docs/analysis/adr/0011-lua-execution-order-authority.md；归档路径 = docs/archive/20261017-1835-adr-0011-lua-execution-order-authority.md。

# ADR-0011：执行顺序的权威归 Lua

> **已废除（2026-10-17 用户裁定）**：vendored Lua / mpack / nlohmann 与 GLKv3 文档路径已从构建和源码树删除，
> 本 ADR 的载体（Lua 沙箱、脚本面、文档解析）不再存在；保留此文仅为历史记录，现行边界见 `docs/plan/MASTER-PLAN.md` §0.1。

- 状态：**Accepted**（**用户裁定** 2026-10-09）
- 日期：2026-10-09
- 取代/延伸：**方案 C**（「脚本是唯一的顺序权威」）；相关 Lua 面见 ADR-0008（运行时沙箱）与 ADR-0010（Lua↔C++ 资源模型）
- 基线：`25322d6d`

## 1 裁定（用户原话）

> legacy profile converter 以后通过模版生成 lua，以后有关执行顺序的 lua 是权威

⇒ 两条：

1. **执行顺序的权威是 Lua**（不是 C++ 里的顺序表、不是 Kotlin 的枚举、不是某份导出的 TSV）；
2. **`LegacyProfileConverter`（v1 → v3 的唯一迁移点）以后由模版生成 Lua**。

## 2 权威归属表（本裁定之后）

| 事实 | 权威 | 其余形态的地位 |
|---|---|---|
| **某条链跑哪些步骤、什么次序** | **Lua** | C++ `contract::kStepSetOrders` 与 `stepset-steps.tsv` = **模版的输入/默认值**，不再是最终解释 |
| 步骤的**名字** | 契约词汇（`StepId` + token） | 导出物（manifest）是导出；Kotlin 枚举是镜像 |
| 步骤的**动作**（写什么、怎么写） | backend（本仓 C++） | Lua 只**点名**，不描述如何写 |
| 一条链**是否接线** | native `ChainSpec.wired` | manifest 的 `available` 列是它的导出 |

## 3 对既有件的影响（逐件）

- **`steps/step_runner.hpp`**（本裁定当日落地）：它**接收**一个顺序并走完 —— 形状不变；但**顺序实参的来源**在 Lua 成为权威后应来自 Lua/计划。其文件注释已按此改写。
- **legacy profile converter**：产出改为**由模版生成的 Lua** ⇒ v1 迁移路径的**终点**从「v3 文档」变为「Lua（或文档 + Lua）」。属 Kotlin 车道，动工前须出设计。
- **`op_registry` / `chain_catalog` / `step_catalog`**：继续作为**词汇与默认值**的权威；它们不再声称持有「顺序」的最终解释权。
- **不改**：步骤名（本会话刚改完）、`StepId` 集合、各 manifest 的导出角色。

## 4 未定项（写明，不猜）

- Lua 与「计划（`op_registry` 的 op token）」的**边界**：顺序在 Lua，而 op 的**参数约束**仍在 `plan_gate` ⇒ 谁验证谁留待下一步；
- 模版的**形状**（生成时机：构建期/导入期）与**校验**（生成的 Lua 是否仍需过 gate）——**未定**，动工前出设计。
