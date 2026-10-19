> 归档说明（2026-10-17）：归档原因 = **载体已删除**（Lua / GLKv3 文档 / script 面 / pipeline-terminator / nlohmann wire_doc / 访问能力层 —— 见 docs/plan/MASTER-PLAN.md §0.1），本文仅作历史记录。原始路径 = docs/analysis/lua-order-design.md；归档路径 = docs/archive/20261017-1835-lua-order-design.md。

# 设计：Lua 如何陈述执行顺序（ADR-0011 的落地面）· v2

> **2026-10-17 作废**：本文的载体（Lua 沙箱、script/wire 面、GLKv3 文档解析、pipeline/terminal 编排、nlohmann wire_doc）已从源码与构建中删除，
> 内容仅作历史记录；现行边界见 `docs/plan/MASTER-PLAN.md` §0.1。

- 状态：**草案 v2**（按 design-critic v0.2 评审修订：N1–N5）
- 日期：2026-10-09 · 基线 `b4548724`
- 权威顺序：守则 > AGENTS > 本文件；**ADR-0011** 是权威（执行顺序归 Lua）

> 修订沿革（哪条评审把我改对了）：见同目录 `lua-order-design-history.md`。

## 1 问题（重述）

ADR-0011：**执行顺序的权威是 Lua**。而本项目「执行顺序」的本体就是三步（`disenforce_selinux` ·
`credential_forgery` · `seccomp_bypass`）⇒ Lua 必须能陈述次序。**两问**：

1. **功能问题 —— 已裁（用户 2026-10-09）：扩词汇** ⇒ **需要单步/子集** ⇒ 走 **ⓐ 扩已声明集合**（新增 step set 行 + op token），**不泛化 runner**。
2. **形态问题（本设计）**：Lua 用什么语法陈述 —— 三形态见 §3。

## 2 约束（更正后）

| # | 约束 | 出处 / 更正 |
|---|---|---|
| C1 | 队列见到即拒 | `profile/run_plan.hpp` · B1 |
| C2 | 计划就是脚本；无计划 ⇒ `plan-missing` | 同上 |
| C3a | **两条机检只查「名以 `window.` 开头」** ⇒ 加步骤名**不触机检** | `script_surface.hpp:363`（N2 更正） |
| C3b | C3 的**理由面**：窗口相位分组不得成为**脚本能力** | 步骤**就是**窗口 op 的内容（`op_registry` 的 token = step set） |
| **C3c** | **词汇里没有单步集合**：`kOpCatalog` 只有 `window.selinux_cred_seccomp` · `window.selinux_cred` · `window.pagecache` · `…_umh`；`kStepSetOrders` 只有三元素/二元素/单步 pagecache ⇒ 要单步**必须同时扩词汇** | `op_registry.hpp:99-107` · `step_catalog.hpp` · `stepset-steps.pin.tsv`（N2 新增） |
| C4 | op 的参数约束由 `plan_gate` 执行 | `op_registry.hpp` |
| C5 | 脚本只能碰 `script_surface` 的名字 | `script_surface.hpp` |
| C6 | 脚本调用被记录（staged），**尚无 op→步骤映射** ⇒ **本设计的落地面** | `script/staged_plan.hpp:10-12/:49`（N4） |

## 3 三个形态

### 形态 C（**推荐**）：**pre_window 的顺序声明**

脚本在 **pre_window** 声明顺序：`order{ steps = [disenforce_selinux, credential_forgery, seccomp_bypass] }`
（`form=Call` · `phase=pre_window` · **`window_allowed=false`**）⇒ 实现层在窗口相位**按该声明执行**。

- **不拼 `window.*`** ⇒ C3a 两条机检**原样通过**；**不是窗口相位动作** ⇒ 不触 C3b 的理由面；
- 可表达**任意子集/次序**（含单步，**前提是 C3c 的词汇允许**）；
- 本树已有「声明 → 实现执行」的先例：`backend` / `close` / `handoff` 都是声明式；`prepare_steps` / `reclaim_plan` 是「顺序/规则写成数据」的先例；`step_runner` 已能接一个 order 走完（头注已写「顺序期望来自 Lua」）。
- **代价（按 N8 的实测精确化：上修但非从零）**：① **新件** —— surface 侧确无字符串/列表 kind（`ArgSpec { key, max(u64), required }`）；② **不是新概念** —— plan/op 侧早有字符串（`ParamWire::Str = 3`，由 plan_gate 校验）；③ **映射现成、载具另有先例** —— `step_from_token` 现成；**文档的数组读取已删**（`51a56231` 删队列读者；`step_queue`/`mpack_node_array` 在 `core/src` 零命中）⇒ 载字符串的真实先例是**脚本→staged 段**（`ArgKind::Str` + arena，上限 4096）；④ **重生成有先例** —— `make -C core stepset-steps-manifest-regen`。⇒ 合计 = 新参数 kind + manifest/pin/两份 doc-check 重生成 + gate 合法性校验 +（可选）词汇扩容。
- **合规论证（N9）**：脚本拼的是 `disenforce_selinux` 等 token —— 属**dispatch 标识符**（用户禁则允许的两类之一），解析靠 `step_from_token` + 已声明的 step set 表 ⇒ **合规**；**不得**把顺序编码成索引数字（那才是「名字→值表」的近似形态）。另：`order` 作为行名**不触** `ORDER_TOKENS` 机检（该机检只扫 `core/pipeline/composition_root.hpp`）。

### 形态 A：三步成为脚本动作

`script_surface` 加三行（`Call`，`window_allowed` 按裁决）⇒ Lua 直接调 `disenforce_selinux(h)` 等。

- 机检**原样通过**（C3a）；真正冲撞的是 **C3b 的理由面** ⇒ 需用户裁决；
- **且单步在词汇上仍不成立**（C3c）⇒ 要单步还得扩词汇 ⇒ **A 并不比 C 更直接**。
- **成本对比（O5 校准）**：A **便宜在参数机制**（只加行名，行可零参数）、**贵在窗口语义**（触 C3b）与需用户裁决；C **贵在参数机制**（surface 需新参数 kind + `stage_gate` 新增校验）、**便宜在语义**（不触 C3b、可表达子集）⇒ 稿中不给 C 任何「改动最小」的暗示。

### 形态 B（**已撤回推荐**）

Lua 调 `backend{ steps=[…] }` ⇒ 需给**冻结的 `backend` 行**加参数 + **新参数 kind** + gate 校验；
且**同样触 C3b 的理由面**（脚本参数决定窗口组合）⇒ 与 A 的冲突同质；**且不能只跑一步**（除非 C3c 扩词汇）。
⇒ 保留为记录，**不作为推荐**。

## 4 裁决顺序（推荐的流程）

1. **功能问题 —— 已裁（用户 2026-10-09）：扩词汇** ⇒ 单步/子集**需要** ⇒ 新增 step set 行（单步集合）+ 对应 op token。
2. **形态 C** 承载该顺序（不触 C3b、机检原样通过）；三步**不**另行成为脚本动作 —— A 仅在用户日后要求「独立动作」时再议。
3. 形态定了，再动 `script_surface` / `op_registry` / `plan_gate` 的同批清单（§5）。

## 5 落地清单（按形态 C 排）

| 件 | 改动 |
|---|---|
| `contract/script_surface.hpp` | 加 `order` 一行（`Call` · `pre_window` · `window_allowed=false`）+ **列表参数 kind**（`ArgSpec` 现在只有数值上限 ⇒ 需扩展） |
| surface manifest / pin / 两份 doc-check | **重生成** |
| `script/stage_gate.hpp` | **脚本参数的校验者就是它** —— 但今天它**只查参数个数**，不查 kind/取值/上限 ⇒ 新参数若不新增校验就是 **vacuous** ⇒ 必须同批加校验 + **可失败判据** |
| `pipeline/plan_gate.hpp` | **看不到 surface 行**（`PlanParam.value` 只有 u64）⇒ 顺序**不进它**；它继续管 op 侧可用性/几何/能力 |
| `contract/op_registry.hpp` | **仅当 C3c 扩词汇**：新增单步/子集 op token ⇒ 与 step set 行同批 |
| `contract/script_surface.hpp`（`backend` 行） | **同批关闭一个既有洞**：该行 `arg_count = 0` ⇒ `stage_gate` 视零参数行为 **free-form（无校验）** ⇒ 今天脚本可给它塞任意参数而无人拦。⇒ 要么**声明**它真正接受的参数，要么**未知参数一律具名拒绝**（fail-closed） |
| `steps/step_runner.hpp` | **不改** —— 用户已裁**扩词汇** ⇒ **不泛化** |
| **禁令（N8）** | 复用文档的 `step_queue` 解析形状时 **不得复活 `queue` 键** —— `queue-removed` 判据在位（C1 仍有效） |
| ADR | 引用 ADR-0011 即可；C3b 的裁决若发生 ⇒ 留沿革 |

> C6 落地面（Lua 顺序如何成为 `StepSetOrder`）与列表承载形态：见沿革件 `lua-order-design-history.md` 的对应小节。

## 7 不在本设计内（不猜）

- 模版生成 Lua 的**时机**与生成物是否仍需过 gate —— ADR-0011 §4 的 open；
- `LegacyProfileConverter` 的改造 —— Kotlin 车道，单独设计。
