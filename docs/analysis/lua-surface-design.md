# Lua 暴露面设计（绑定面 = **实际暴露什么就绑什么**）

> 分析类（≤8 KiB）。**自包含**。**口径（用户 2026-10-08 修正）**：绑定面**不是**「只能 `contract`」，而是「**我们实际暴露什么就绑什么**」⇒ 先有**暴露面表**（唯一权威），再按表绑定；**表外名字 ⇒ 具名拒绝**。
> 相关：[ADR-0008](../analysis/adr/0008-lua-runtime-sandbox.md) · [ADR-0010](../analysis/adr/0010-lua-cpp-resource-model.md)（**待批**）· [P5 规格](../analysis/handoff-script-layer-p5-spec.md) · [rewrite-plan-v2](../plan/rewrite-plan-v2.md)（A6）。

## §1 暴露面表（**唯一权威**；表外名字一律拒绝）

| 条目名 | 形态 | 参数界 | 语义 | 实现层 | **stage 期** | 执行期 | 相位归属 | 持资源 | 拒绝码 |
|---|---|---|---|---|---|---|---|---|---|
| `backend(id)` | 函数 `(id:str) → backend 句柄` | `id ∈` 目录 token 集 | 取 backend 句柄（仅作后续条目接收者） | 组合根 / 目录 | 允许（只记录） | 允许 | **否** | 否（句柄=目录项） | `surface-unknown-token` · `backend-not-available` |
| `b:writeop{target, value, width?}` | 句柄方法（table 参数） | **`target ∈ profile 符号名`** · `value` 整数 · `width? ∈ {1,2,4,8}` | **窗口内原子写**（park → write → release 由实现层一次完成）；**相位 = 运行时内部** | backend route / step | 允许（stage 记录） | 允许 | **窗口相位**（运行时内部分组） | 否（句柄只标识） | `param-invalid` · `geometry-missing` · `op-not-available` |
| `b:settle{us}` | 句柄方法 | `us` 整数 ≤ profile 参数上界 | 时间等待（**参数化**；不得用 C++ 默认值兜底） | `support/timing` | 允许 | 允许 | **是**（值由参数化控制） | 否 | `param-invalid` · `param-missing` |
| `b:handoff{terminal}` | 句柄方法 | `terminal ∈` 目录 token 集 | 交接给 terminal（**收尾三声明**：`on_success`/`always`/`on_failure`，见 §4.1） | terminal / handoff | 允许 | 允许 | **否** | 否 | `surface-unknown-token` · `executor-unavailable` |
| **`run`** | 表（table 参数） | **`argv` 或 `shell`（二者必居其一）** · `capture?`（bool） · `timeout_ms?`（≤ profile 上界） | 执行命令；**`argv` 与 `shell` 同时缺省/同时给出 ⇒ 具名拒绝** | terminal | 允许 | 允许 | **terminal 相位（root 后）**；**窗口内否** | 否 | **`run-form-invalid`** · `param-invalid` |
| **`cfg`**（只读快照） | 表（快照） | 字段 ∈ manifest 声明集合 | **脚本可读的只读快照**（取代 `profile.get`） | profile 层（快照注入） | 允许 | 只读允许 | 否 | 否 | `cfg-unknown-field` |
| 语言级循环 / 条件（`for`/`while`/`if`） | **语言结构**（非条目） | 迭代次数受**预算**约束 | 控制流（录制可展开、重放须一致） | Lua 沙箱 | 允许 | 允许 | **否**（窗口内零 Lua） | 否 | `budget-exceeded` |
| **表外任意名字** | — | — | — | — | **拒绝** | **拒绝** | — | — | **`surface-unknown-name`**（附 `path` 与 hint） |

- **列语义**：**实现层** = 条目落到的层；**stage 期/执行期** = 单遍（§3）两段是否放行；**相位归属** = 运行时**内部**相位分组（**非**脚本可见手段）；**持资源** = 一律否。
- **窗口级 op 已降为运行时内部**：脚本**不表达**窗口分组；由实现层按条目类型内部分组（`writeop`/`settle` 属窗口相位）。
- **`profile.get` 已删**（被 `cfg` 取代）：只读快照、stage 前注入，只能读字段。

## §2 绑定权威（**单一名单**）

- **持有者**：新契约层 `surface` 表（`constexpr`）= **唯一权威**；宿主**按表注册**（集合 ≠ 表 ⇒ 启动期失败）。
- **机器判据**：① 注册集合 == 表（逐项断言）；② 全仓 grep：条目名**只定义一次**；③ 沙箱**符号级证据**（表外名字不存在）。
- **判据形式**：表外调用 ⇒ **具名拒绝** `surface-unknown-name`（**默认拒绝**，不做近似匹配）。
- **一致性**：表与绑定**同源**（表驱动生成）；「先写绑定再补表」= 违例。

## §3 单遍执行（**跑脚本 → stage → gate 校验 → 按相位执行**）

1. **跑脚本（一次）**：窗口外运行一次 ⇒ **staged 计划**（条目序 + 参数）。
2. **gate 校验（对象 = staged）**：`plan_gate` 十条 + surface 表（名字/参数界）**针对 staged**；**不重跑脚本**；失败 ⇒ 具名拒绝。
3. **执行（按相位）**：按 staged **按相位**执行（分组由实现层内部定）；**脚本不参与执行**。

- **逐调用边界校验（保留）**：每次调用校验参数界/句柄/相位；越界 ⇒ 具名拒绝 + 立即终止。
- **判据（三条，可证伪）**：
  - **stage 零副作用**：stage 期无写系统调用/资源获取（证据 = 符号级 + 观测）；
  - **确定性**：同输入两次 stage **逐字节相同**；
  - **staged == 执行**：逐条逐值一致；偏差 ⇒ 立即终止 + 记缺陷。

## §3.1 纪律放宽记录（**强制越权，TD-RW2-1**）

- **裁定**：「用户脚本正确性 = 用户责任」。
- **放宽**：**不再**为脚本逻辑错误做静态证明。
- **保留**：**窗口内零 Lua** · **逐调用边界校验** · **gate 校验 staged** · **预算**。
- **代价**：脚本改坏**可能 panic**（本层不拦）。
- **技术债**：**`TD-RW2-1`**；**偿还时机 = 首个真机门禁（G2）后复评**（按真机证据决定是否加回）。
- **`TD-RW2-2`（`run` 双形态）**：允许 `shell` ⇒ **命令级白名单失效**；**剩余界** = **相位**（仅 terminal 相位、root 后）+ **预算**（指令/内存/timeout）+ **审计**（命令与输出入日志）；**代价（如实）** = root 后**可执行任意命令**；**偿还时机 = 首个真机门禁（G2）后复评**。

## §4 资源与窗口

- **句柄边界**：脚本只持**表内条目**的句柄（`backend(id)` 返回）；句柄 = POD `{kind,index,generation}` · 有界 64 · 无 `__gc`（[ADR-0010](../analysis/adr/0010-lua-cpp-resource-model.md) **待批**）；不得自造/延长/跨域。
- **窗口内**：**脚本本身在窗口内不执行**（窗口内零 Lua）；窗口相位动作由实现层按 **staged 计划**执行（`writeop`/`settle`）；其余条目**不得**出现在窗口相位。
- **资源归属**：窗口资源由**窗口宿主**独占（RAII 逐路径回收）；缺参数 ⇒ 具名拒绝（不兜底，见 [rewrite-plan-v2](../plan/rewrite-plan-v2.md) §3）。

## §4.1 三层收尾（声明式）

- **收尾三声明（脚本可见）**：`terminal{on_success, always, on_failure}` —— **声明式**，取代「模板 + exec 列表 + always 条目」过程式拼装。
- **内核兜底留 C++**：relay/enforce 恢复与 watchdog **留在 C++**（不由脚本表达）。
- **被取代（沿革不删）**：旧 **`root_cmd` 模板被取代**（命名/语义仍作输入）。

## §5 取代关系（**沿革不删**）

| 旧表述 | 新表述（本文） | 沿革 |
|---|---|---|
| P5：**计划 = `return` 表** | **计划 = 脚本（stage 出计划）** | 脚本仍是顺序唯一权威；形态由「返回表」改「**staged 计划**」 |
| AGENTS：`available{route,queue}` | **`available` 列表**（`description` + 脚本） | 菜单项不再带 `route`/`queue`；顺序/参数由脚本表达（queue 已退役） |

- **保留**：旧表述与决策（D22/D30/D31、R11 选 C）**不删**；本节只标注**取代关系**。

## §6 待用户裁定（三条）

| # | 待裁定 | 选项 | 保守默认 |
|---|---|---|---|
| **Q1** | `b:settle` 是否可进窗口 | ① 允许（值受上界约束）② 仅窗口外 | **①**（`us` 来自 profile） |
| **Q2** | `b:writeop.width` 缺省 | ① 缺省即拒（`param-missing`）② 取 profile | **①**（与「数值不入 C++」一致） |
| **Q3** | `available` 项脚本承载 | ① 资产路径 ② 内联 | 待定（随 A3/A6 资产形态） |
