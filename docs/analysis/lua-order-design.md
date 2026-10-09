# 设计：Lua 如何陈述执行顺序（ADR-0011 的落地面）· v2

- 状态：**草案 v2**（按 design-critic v0.2 评审修订：N1–N5）
- 日期：2026-10-09 · 基线 `b4548724`
- 权威顺序：守则 > AGENTS > 本文件；**ADR-0011** 是权威（执行顺序归 Lua）

> 修订沿革（哪条评审把我改对了）：见同目录 `lua-order-design-history.md`。

## 1 问题（重述）

ADR-0011：**执行顺序的权威是 Lua**。而本项目「执行顺序」的本体就是三步（`disenforce_selinux` ·
`credential_forgery` · `seccomp_bypass`）⇒ Lua 必须能陈述次序。**两问**：

1. **功能问题（交用户）**：是否需要**单步 / 任意子集**？（而不是只有整链）
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
- **代价（按 N8 的实测精确化：上修但非从零）**：① **新件** —— surface 侧确无字符串/列表 kind（`ArgSpec { key, max(u64), required }`）；② **不是新概念** —— plan/op 侧早有字符串（`ParamWire::Str = 3`，由 plan_gate 校验）；③ **解析现成** —— `step_from_token`（token→`StepId`，其注释正为「文档拼的步骤与计划写的步骤按同一表匹配」而设）与文档的 token 数组读取可复用；④ **重生成有先例** —— `make -C src stepset-steps-manifest-regen`。⇒ 合计 = 新参数 kind + manifest/pin/两份 doc-check 重生成 + gate 合法性校验 +（可选）词汇扩容。
- **合规论证（N9）**：脚本拼的是 `disenforce_selinux` 等 token —— 属**dispatch 标识符**（用户禁则允许的两类之一），解析靠 `step_from_token` + 已声明的 step set 表 ⇒ **合规**；**不得**把顺序编码成索引数字（那才是「名字→值表」的近似形态）。另：`order` 作为行名**不触** `ORDER_TOKENS` 机检（该机检只扫 `core/pipeline/composition_root.hpp`）。

### 形态 A：三步成为脚本动作

`script_surface` 加三行（`Call`，`window_allowed` 按裁决）⇒ Lua 直接调 `disenforce_selinux(h)` 等。

- 机检**原样通过**（C3a）；真正冲撞的是 **C3b 的理由面** ⇒ 需用户裁决；
- **且单步在词汇上仍不成立**（C3c）⇒ 要单步还得扩词汇 ⇒ **A 并不比 C 更直接**。

### 形态 B（**已撤回推荐**）

Lua 调 `backend{ steps=[…] }` ⇒ 需给**冻结的 `backend` 行**加参数 + **新参数 kind** + gate 校验；
且**同样触 C3b 的理由面**（脚本参数决定窗口组合）⇒ 与 A 的冲突同质；**且不能只跑一步**（除非 C3c 扩词汇）。
⇒ 保留为记录，**不作为推荐**。

## 4 裁决顺序（推荐的流程）

1. **用户先裁 C3c 的功能问题**：是否需要**单步 / 任意子集**？
2. 无论答案 ⇒ **形态 C** 都可用（差别只在 C3c 是否扩词汇）；若用户要求三步作为**独立动作** ⇒ 再叠 **A**。
3. 形态定了，再动 `script_surface` / `op_registry` / `plan_gate` 的同批清单（§5）。

## 5 落地清单（按形态 C 排）

| 件 | 改动 |
|---|---|
| `contract/script_surface.hpp` | 加 `order` 一行（`Call` · `pre_window` · `window_allowed=false`）+ **列表参数 kind**（`ArgSpec` 现在只有数值上限 ⇒ 需扩展） |
| surface manifest / pin / 两份 doc-check | **重生成** |
| `pipeline/plan_gate.hpp` | 校验 `order.steps` 的合法性（见 §6） |
| `contract/op_registry.hpp` | **仅当 C3c 扩词汇**：新增单步/子集 op token ⇒ 与 step set 行同批 |
| `steps/step_runner.hpp` | **不改**（接一个 order 走完），**除非** §6 结论要求泛化 |
| **禁令（N8）** | 复用文档的 `step_queue` 解析形状时 **不得复活 `queue` 键** —— `queue-removed` 判据在位（C1 仍有效） |
| ADR | 引用 ADR-0011 即可；C3b 的裁决若发生 ⇒ 留沿革 |

## 6 C6：Lua 给的顺序**如何成为** `StepSetOrder`（本设计必须覆盖）

两条路，**选定其一**（决定 `step_runner` 是否泛化）：

- **匹配已声明集合**：`order.steps` 必须**恰好**等于某个已声明 step set（顺序一致）⇒ 实现只做查表 ⇒ `step_runner` **不改**；单步/新子集必须**先扩词汇**（C3c）。
- **允许任意 span**：`order.steps` 可以是任意子集/次序 ⇒ 需要一个新的运行期结构（不是 `StepSetOrder`）⇒ `step_runner` 要**泛化**（接一个 span 而不是目录行）。
  **（N6 重要项）** 走这条路**必须同时定义「合法 span 集合」的数据表**（谁声明 · 何处存放 · 如何 pin · 不匹配 ⇒ **具名拒绝**）—— 否则「哪种次序在机制上合法」就成了 plan_gate 代码里的知识，与 `prepare_steps`/`reclaim_plan` 的「顺序/规则写成数据 + 编译期 pin」先例相抵，靠推断即违「不猜」。**不想引入该表 ⇒ 就走第三条路**。
- **第三条路（N7）：声明序列 + 精确规范化 + 不匹配即拒** —— 其规范形式是「**扩已声明集合**」：每加一种合法次序就加**一行 step set**（数据 + pin，与既有先例同形），脚本语法不变，运行时**只查表** ⇒ `step_runner` **不改**。
  两条约束：① **规范化必须精确**（近似/就近匹配 = 推断 ⇒ 违规）；② 「允许任意前缀」是可选项，但同样要有机制依据（例如 `selinux_cred` 已是被声明的合法二步集），**实现层不得自行截断**。
  ⇒ 第三条**不是捷径**，它是「扩声明集合」的规整表述 ✓。

⇒ 因此要「单步」**只有两条真路**：**ⓐ 扩已声明集合**（= 第三条路的规范形式）或 **ⓑ 泛化 runner**（= span 路）。
本稿**不替用户选**：这与 C3c 的功能问题**是同一个问题的两面**。

## 7 不在本设计内（不猜）

- 模版生成 Lua 的**时机**与生成物是否仍需过 gate —— ADR-0011 §4 的 open；
- `LegacyProfileConverter` 的改造 —— Kotlin 车道，单独设计。
