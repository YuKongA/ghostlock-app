# 设计：Lua 如何陈述执行顺序（ADR-0011 的落地面）· v2

- 状态：**草案 v2**（按 design-critic v0.2 评审修订：N1–N5）
- 日期：2026-10-09 · 基线 `b4548724`
- 权威顺序：守则 > AGENTS > 本文件；**ADR-0011** 是权威（执行顺序归 Lua）

## 0 修订记录（哪条把我改对了）

| 评审项 | 我原来的说法 | 更正 |
|---|---|---|
| **N1 阻断** | 推荐形态 B，理由「唯一不冲突 + 改动最小」 | **两条都不成立**：B 同样触 C3 的**理由面**；且 `backend` 行**零参数**、`ArgSpec` **只有数值上限**、`steps=[…]` 需要**新参数 kind**；§5 的「参数进 ParamSpec」是**范畴错误**（`ParamSpec` 属 op 行，`backend` 是 surface 行） |
| **N2** | 「A 与 C3 直接冲突 ⇒ 两条机检同批改写」 | 机检**只查 `window.` 前缀** ⇒ A 加步骤名**原样通过**；而且**更硬的约束漏了**：词汇里**根本没有单步集合** |
| **N3** | 「A3-2 门禁里出现过单步用法」 | **无物证**（那条日志的 `step=0` 是 route 自己的状态计数）⇒ **撤回** |
| **N4** | C6 只列为约束 | **C6 是落地面本体**，本设计必须覆盖「Lua 给的顺序如何成为 `StepSetOrder`」 |
| **N5** | 只有 A/B 两形态 | **存在第三形态且支配 A/B**：**pre_window 的顺序声明** |

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
- **代价**：一处新行 + **一个列表参数 kind** + gate 的顺序合法性校验（与 B 同量级，但**功能严格更强**）。

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
| ADR | 引用 ADR-0011 即可；C3b 的裁决若发生 ⇒ 留沿革 |

## 6 C6：Lua 给的顺序**如何成为** `StepSetOrder`（本设计必须覆盖）

两条路，**选定其一**（决定 `step_runner` 是否泛化）：

- **匹配已声明集合**：`order.steps` 必须**恰好**等于某个已声明 step set（顺序一致）⇒ 实现只做查表 ⇒ `step_runner` **不改**；单步/新子集必须**先扩词汇**（C3c）。
- **允许任意 span**：`order.steps` 可以是任意子集/次序 ⇒ 需要一个新的运行期结构（不是 `StepSetOrder`）⇒ `step_runner` 要**泛化**（接一个 span 而不是目录行）。

⇒ 本稿**不替用户选**：这与 C3c 的功能问题**是同一个问题的两面**（要单步 ⇒ 必然 `span` 或扩词汇二选一）。

## 7 不在本设计内（不猜）

- 模版生成 Lua 的**时机**与生成物是否仍需过 gate —— ADR-0011 §4 的 open；
- `LegacyProfileConverter` 的改造 —— Kotlin 车道，单独设计。
