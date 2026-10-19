> 归档说明（2026-10-17）：归档原因 = **载体已删除**（Lua / GLKv3 文档 / script 面 / pipeline-terminator / nlohmann wire_doc / 访问能力层 —— 见 docs/plan/MASTER-PLAN.md §0.1），本文仅作历史记录。原始路径 = docs/analysis/handoff-script-layer-p5-spec.md；归档路径 = docs/archive/20261017-1835-handoff-script-layer-p5-spec.md。

# P5 设计规格：handoff / 脚本计划 / 插件层

> **2026-10-17 作废**：本文的载体（Lua 沙箱、script/wire 面、GLKv3 文档解析、pipeline/terminal 编排、nlohmann wire_doc）已从源码与构建中删除，
> 内容仅作历史记录；现行边界见 `docs/plan/MASTER-PLAN.md` §0.1。

> 分析类（≤8 KiB）。**自包含**。P5 唯一权威规格（[rewrite-plan](../plan/rewrite-plan.md) §5）。判据形式 = D1（黑盒/白盒 + 极端值）。
> **2026-10-08 部分取代（本行优先）**：脚本计划的**当前权威 = [lua-surface-design](lua-surface-design.md)**（+ `lua-surface-annex-document` / `-annex-target` / `-annex-route-switch`）—— 含：**单遍 stage**（取代本文『录制 → 重放』表述）· **`run` 移除**（收尾命令定义权归 Kotlin，见 annex-document）· **暴露面 7 条**（`backend` / `close` / `writeop` / `copyop` / `settle` / `handoff`（引用语义）/ `cfg`）· **统一拒绝码**（取代本文旧码表；权威见新 UML §3.2）。**本件保留为输入与沿革**；与之冲突时**以 lua-surface-design 为准**。
> 上游权威：[script-plan-design](script-plan-design.md)（queue 退役 / 计划=脚本 / 内置脚本 / `plan-missing`）· [ADR-0008](adr/0008-lua-runtime-sandbox.md)（沙箱）· [ADR-0009](adr/0009-script-trust-and-hardline.md)（硬线 D29）· [ADR-0010](adr/0010-lua-cpp-resource-model.md)（资源模型）· [b1b2](handoff-payload-b1b2-runtime.md) §B2.1–B2.5 · [P4](composition-layer-p4-spec.md)（OpRegistry / plan_gate / AttackWindow）。

## §1 组件与边界（逐条「只做 / 不做」）

| 组件 | 只做 | **不做** |
|---|---|---|
| **脚本宿主**（ScriptHost） | 解释器实例 / 沙箱 / 预算 / **句柄表**的**唯一持有者**；装载脚本、按白名单暴露 API、指令预算与内存上限、越界即终止 | 不读配置、**不决定顺序**（顺序只由脚本正文表达）、不持有窗口作用域资源、不注册 `io`/`os`/`package`/`debug`/`load`/`dofile`/`require` |
| **计划装载与校验**（PlanLoader） | 选择**生效脚本**（内置计划脚本资产 / 文档自带脚本）；大小 / sha256 / 语法 / 预算声明的校验；无脚本且无内置件 ⇒ `plan-missing` | **不做能力判定**（归 `plan_gate`）、不执行、不排序、不合并两份计划 |
| **执行器集合**（ExecutorPolicy：`UserRoot` / `Umh` / `CredGrant` / `KernelCmdSet`） | `kind` / `available(facts)` / `run(plan)`；**构建期四选一** | **不做运行期换执行器**（禁止回退：失败即终止） |
| **handoff 组装**（HandoffPlanBuilder） | 模板 + exec 列表 + `always` 语义 + **三层收尾**（模板后段 → always 条目 → 内核兜底）；App 侧产出**字节 + sha256**，native 侧落盘 | 不留运行期决策；不在攻击中改写收尾层 |

- **执行器依据**：`handoff-payload-plan.md:20`（四个实现）+ `:112`（D6 构建期四选一，UMH 不可用⇒CredGrant）+ `handoff-payload-kotlin.md:102-105`（词汇映射）。
- **三层收尾依据**：`handoff-payload-plan.md:48`（模板后段/always/内核兜底）+ `:93`（P4 收尾 = 三层保证 + 后置条件核验）。

## §2 单一权威

1. **计划 = 脚本**：脚本是**顺序的唯一权威**；**不得存在第二处顺序来源**。
2. **queue 已退役**（[script-plan-design](script-plan-design.md) §1）：`backend.<id>.queue` **出现即拒**（`queue-removed`，附 `path` 与 hint）。
3. **内置计划脚本与用户脚本 = 覆盖，不是并存**：一次运行**只有一份生效脚本**（用户脚本覆盖内置件）；**不做两份计划的合并/拼接**。
4. **句柄只能由宿主探针在窗口外产生**（[ADR-0010](adr/0010-lua-cpp-resource-model.md) §3 规则①）：脚本**不得**构造、修改、延长或推导句柄。

## §3 与 P2 / P4 的接口

> 脚本调用如何下落为 op（**三条规则**）见 [op-fall-rule.md](op-fall-rule.md)（原指针指向不存在的 §「下落规则」，2026-10-08 更正）。

- **唯一通路**：脚本调用（**`contract::kSurface` 的 7 条**，用户批准）→ staged 计划（surface 条目）→ **下落**为 `OpRegistry` 的 op 条目（**仅计划侧**，见 [lua-surface-design](lua-surface-design.md)）→ `plan_gate`；**到 op 只有这一条通路**，且 **`op(...)`/`window.*` 不是脚本可见名**（冻结判据：`surface_has_no_window_prefixed_names()` + `make -C core check` 的 grep）。token 必须 ∈ `OpRegistry`（[P4](composition-layer-p4-spec.md) §2）。
- **`plan_gate` 调用点**：`state_from` **之后**、`Backend::run` **之前**（[P4](composition-layer-p4-spec.md) §3 / `b1b2 §B1.2`）；gate **只做二值判定**、只读 `DeviceFacts`。
- **窗口内零 Lua**：由 `AttackWindow` **结构性**保证（该层不引用 Lua 运行时）；`landing` **纯静态**（只能声明在操作之间）；运行期一旦观测到窗口内调用 ⇒ 缺陷 `landing-in-pi-window`。
- **wire 通道**：脚本正文 **>256 B**（`kMaxStringBytes`）⇒ 必须走 `bin`；单脚本插入段 **≤64 KB**；其余键仍拒 `bin`（`b1b2 §B2.5`）。

## §4 判据（可证伪 + 极端值）

- **白盒**：

  - **沙箱未注册**（**符号级证据**）：链接产物/符号表中**不含** `io`/`os`/`package`/`debug`/`load`/`dofile`/`require`（`luaopen_*` 白名单比对，逐项可核）。
  - **句柄表有界 64** + **代际回绕后旧句柄失效**（`handle-stale`）。
  - **预算超限即终止**（`lua_sethook` 指令预算 + `lua_setallocf` 内存上限；[ADR-0008](adr/0008-lua-runtime-sandbox.md) §3 参数为**执行期硬约束、不得由 profile 放宽**）。

- **黑盒（具名拒绝 + 立即终止）**：`handle-stale` / `handle-kind-mismatch` / `handle-out-of-range` / `handle-scope-denied` / `param-invalid`（[ADR-0010](adr/0010-lua-cpp-resource-model.md) §4）+ `queue-removed` + `plan-missing`。
- **证伪（必须能失败；**指定测试** = P5 实现批的 `sandbox_api_test` / `handle_test`）**：① **放开 `load`/`dofile` 之一** ⇒ `sandbox_api_test` **必须失败**（「未注册符号」断言红）；② **去掉代际检查** ⇒ `handle_test` 的**旧句柄用例必须失败**（`handle-stale` 不再触发）；两者实验后**还原 + `rm -f` 产物 + 重建复核**。
- **极端值**：空脚本 · 语法错 · 超预算 · **表超深** · **64 句柄用满** · **跨插件句柄** · **脚本内 queue 残留**。

## §5 顺序与反工时点

| 步 | 内容 | 反工时点（晚做会返工） |
|---|---|---|
| 1 | **沙箱与句柄**（安全面） | 晚做 ⇒ 安全面回归、后面所有 API 都要重新过沙箱 |
| 2 | **计划装载**（生效脚本/覆盖/`plan-missing`） | 晚做 ⇒ handoff 模板与校验要按错的前提写 |
| 3 | **执行器集合**（四选一） | 晚做 ⇒ `executor-unavailable` 判据（gate 第 10 条）无事实来源 |
| 4 | **handoff 组装**（模板/exec 列表/三层收尾） | 先做 ⇒ 计划面未定即写模板 ⇒ **整体重写** |

## §6 未决

| # | 未决项 | 依赖 |
|---|---|---|
| U-1 | **R10（数据形态）**对脚本**读取面**的影响（脚本能读到哪些字段、默认值语义） | R10 裁定（P3） |
| U-2 | **插件解冻时机**（D31/D32 形态已定但**仍冻结**，`.so` 仅作内核型插件保留通道） | 用户解冻指令（`handoff-plugin-lua.md:49`） |
| U-3 | **内置计划脚本的资产路径与命名**（`plan-<backend>-<route>.lua` 的落点） | 与 P3 的资产形态一致 |
| U-4 | **E2E-1 中脚本通道的判据落点**（脚本如何进入 host 全流程用例） | [host-assembled-integration-plan](host-assembled-integration-plan.md) §2 |

## §7 取代关系与沿革

- **整节已迁出**（配额）⇒ 见 **[handoff-script-layer-p5-spec-annex.md](handoff-script-layer-p5-spec-annex.md)**（取代/不取代/沿革/取代纪律四条，内容未改）。
