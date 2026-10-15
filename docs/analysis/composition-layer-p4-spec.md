# P4 设计规格：装配层与窗口宿主（`composition_root` + `AttackWindow`）

> 分析类（≤8 KiB）。**自包含**。P4 唯一权威规格。判据形式 = D1（黑盒/白盒 + 极端值）。
> 相关：[P2 契约层](contract-layer-p2-spec.md) · [窗口约束](pi-window-constraints.md) · [脚本计划](script-plan-design.md) · [重写计划](../plan/rewrite-plan.md) §5 的 P4。

## §1 两个后继组件（旧名 → 新名，职责边界）

| 组件 | 前身 | **只做** | **不做**（边界） |
|---|---|---|---|
| **`composition_root`** | `pipeline/orchestrator.hpp.disabled` | **实例化 + 构造绑定 + 编译期校验** | **不决定顺序**；不持有窗口资源；不排序/调度/重试 |
| **`AttackWindow`** | `pipeline/pipeline.hpp.disabled` | **窗口资源唯一所有者**；**RAII 每条退出路径回收**；`static_assert` 挂载点 | 不解释脚本、不读配置、不做策略选择 |

- **依据**：`b1b2` §B1.1/§B2.2 + R2（`orchestrator` → `composition_root`，顺序归计划）。
- **窗口作用域证据**：`core/src/backend/cve_2026_43499_state.hpp:25-40` —— `profile` / `addresses` / `heap` / `race` / `victim` / `parked_victim` / `parked_victim_cmd` 均为 **state 成员**；其构造/析构沿用旧 `pipeline.hpp:26-46` 的 **RAII guard** 形态（`state_construct` → … → `state_destroy`）。
- **判别标准**：资源**在 waiter 存活期内可达**且**生命周期由宿主决定**（脚本只持**句柄**）⇒ **窗口作用域**；op 内临时对象 ⇒ op 作用域；Lua 表/字符串 ⇒ 脚本作用域（[ADR-0010](adr/0010-lua-cpp-resource-model.md)）。
- **`static_assert` 挂载点**：`AttackWindow` 是所有窗口期不变量的**唯一**落点（槽尺寸、资源不重叠、退出路径完整性），不得分散到脚本层或 backend。

## §2 `OpRegistry` / `OpSpec` = **脚本唯一接口权威**

- **类型**（`b1b2 §B1.1`）：`ParamSpec{key,wire,required,max}` / `OpSpec{OpId,token,RouteMask,params,requires,effects,produces,consumes}` / `OpRegistry`（**constexpr 表，无运行期注册**）。
- **编译期保证**：`OpId` 唯一 · `routes ⊆` backend 可用 route · `produces/consumes` 的 kind 有定义。
- **取代关系**：本组类型**取代** `contract::StepExecution`（旧步骤注册点）—— 旧概念划掉。
- **唯一权威判据**：脚本 `op(token)` 的 token **必须** ∈ `OpRegistry`；**不得**存在第二处 token 名单（含文档、wire、Kotlin 侧硬编码）。
- **参数上界**：`ParamSpec.max` 是窗口内参数的**唯一**上界来源（gate 依此判 `param-invalid`）。

## §3 `plan_gate`：十条 + 两条（R11 新增）

- **十条静态拒绝**（`b1b2 §B1.2`）：`unknown-op` / `op-not-available` / `route-not-available` / `geometry-missing` / `capability-missing` / `order-violation` / `hash-mismatch` / `param-invalid` / `landing-in-pi-window` / `executor-unavailable`。
- **R11 新增两条**（[script-plan-design](script-plan-design.md)）：**`queue-removed`**（`backend.<id>.queue` 出现即拒）· **`plan-missing`**（既无脚本又无内置计划脚本）。
- **返回结构**：`GateVerdict{ok, reason, path, index}`；`path` 语法 = `backend.<id>` → `.op[<index>]`（0 基）→ `.<field>`；**`index` 与 `[i]` 必须一致**（用例断言）。
- **插入点**：`state_from` 成功之后、`Backend::run` **之前**（需要几何，不能更早）；**只做二值判定**，不产生备选（**禁止运行期回退**）。
- **去重**：`selection_supported` / `combination_available` **并入** gate；同一事实**不得两处判定**。
- **`DeviceFacts`**：由 `composition_root` 探测后、进窗口前**填充并冻结**；gate **只读、不探测**；`capability-missing` 来源 = `contract::Capabilities`（中性 POD）。
- **判定优先级（单一权威表）、空计划语义、第 4 条 op**：见 [附件](composition-layer-p4-spec-annex.md) §A1–§A3（测试按表断言，顺序不改）。

## §4 窗口原子性 · 参数化 · 窗口内零 Lua

1. **原子性**：窗口内多步（park → write×N → release）**必须是一个 op**；脚本只能编排 **op 之间**（`b1b2:78`「landing 只能声明在操作之间」）。
2. **参数化**：窗口内可变性**一律参数化**（先例 `execution-tuning.conf:20-36` + `steps.cpp:77/:238` 的 attempts 循环）；**拆分窗口不提供额外自定义**，只带来四条代价（窗口内 Lua / 新增代码路径 / 窗口拉长 / 失败态组合爆炸）。
3. **窗口内零 Lua**：由 `AttackWindow` **结构性保证**（该层不引用 Lua 运行时；`landing` 纯静态；运行期一旦观测到窗口内调用即按缺陷 `landing-in-pi-window`）。

## §5 六条约束（含**诚实边界**）

| # | 约束 | 判据形态 |
|---|---|---|
| C1 | **所有权唯一**：窗口资源只由 `AttackWindow` 拥有 | 白盒：无第二处持有/释放 |
| C2 | **顺序归计划**：`composition_root` **不排序** | 白盒：无顺序常量/分支 |
| C3 | **窗口原子性**：多步 = 一个 op | 黑盒：拆分 ⇒ 具名拒绝 |
| C4 | **窗口内零 Lua** | 白盒：层内零 Lua 引用 |
| C5 | **参数化 + 上界**：`ParamSpec.max` 唯一上界 | 黑盒：上界+1 ⇒ `param-invalid` |
| C6 | **单一权威**：无第二读取路径 | 白盒：grep 计数 0 |

- **诚实边界（不承诺）**：只保证「op 名合法 · 参数合法 · 能力上界 · 预算内终止 · 窗口内零 Lua」；**不承诺**顺序正确（不可静态判定）；**不覆盖**内核行为/PI 时序 ⇒ 输出**不是真机判据**。

## §6 R1 层表登记

- **新层**：`pipeline`（`composition_root.*` + `attack_window.*`）；允许 include `contract` / `session` / `support` / `memory` / `profile`；**不得**被 `backend/**` include（backend 只依赖 P2 兼容面）。
- **`AttackWindow` 是唯一允许持有窗口作用域资源的层**；其他层只能持有**句柄**。
- **R1 防火墙**：白名单**保持 0 条**（空账本 + pin）；新增层名与边须**同批**登记；层边与禁用前**等价**（重写不得放宽）。

## §7 可证伪判据 + 极端值

- **白盒**：`composition_root` 内**无顺序分支**（顺序常量 grep = 0）；`AttackWindow` 的**每条退出路径**（成功/失败/重试/取消/提前返回）都有回收 guard —— 判据 = **枚举退出路径清单 + 逐条断言**。
- **黑盒**：任一被禁行为 ⇒ **具名拒绝且立即终止**（`landing-in-pi-window` / 超预算 / 无效句柄 / `queue-removed` / `plan-missing`）。
- **证伪（必须能失败）**：① 在窗口内插入一次 Lua 调用 ⇒ **指定测试必须失败**；② 让 `composition_root` 对 op 排序 ⇒ **顺序判据测试必须失败**；实验后**还原 + `rm -f` 产物重建复核**。
- **极端值**：空计划 · 单 op · 64 句柄用满 · 代际回绕 · 参数 = 上界与上界+1 · **窗口内多步拆成两个 op（应拒绝）** · 计划引用不存在 op。

## §8 实现顺序与反工时点

| 步 | 内容 | 反工时点 |
|---|---|---|
| 1 | 冻结 §1 判别标准（窗口/op/脚本三分类） | 不先冻结 ⇒ 资源归属反复改 |
| 2 | `AttackWindow`（RAII 骨架 + `static_assert` 挂载点） | 晚做 ⇒ 退出路径漏回收（历史 panic 教训） |
| 3 | `composition_root`（实例化 + 构造绑定） | 若把顺序写进来 ⇒ 与计划**双权威**（M5 级风险） |
| 4 | `OpRegistry` 接线（token 唯一来源） | 先写 gate 后定 registry ⇒ token 面**重写** |
| 5 | `plan_gate` 十条 + 两条 | 晚做 ⇒ 非法输入在运行期才暴露 |
| 6 | 判据 + 证伪实验 | 无证伪 ⇒ 守卫可能 vacuous |

## §9 未决（对 P3 / R10 的依赖）

| # | 未决项 | 依赖 |
|---|---|---|
| U-1 | 默认值/必填范围 ⇒ 影响 `param-invalid` 的判定口径 | **R10 裁定**（P3 数据形态） |
| U-2 | `DeviceFacts` 的探测时机与来源 | P3（会话/配置层） |
| U-3 | 内置计划脚本的装载路径（App 资产 vs 文档内嵌） | P3 + R11 |
| U-4 | `executor-unavailable` 的可用性判据 | P3（终端/执行器面） |
