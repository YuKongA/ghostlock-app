# 设计：Lua 如何陈述执行顺序（ADR-0011 的落地面）

- 状态：**草案**（待 design-critic 评审 → 用户裁决）
- 日期：2026-10-09 · 基线 `28c01ad8`
- 权威顺序：守则 > AGENTS > 本文件；**ADR-0011** 是权威（执行顺序归 Lua）

## 1 问题（一句话）

ADR-0011 说「执行顺序的权威是 Lua」—— 而**三步**（`disenforce_selinux` · `credential_forgery` · `seccomp_bypass`）
正是本项目里「执行顺序」的本体 ⇒ **Lua 必须能说出这三步及其次序**。
问题是**以什么语法陈述** —— 本文件给两个形态、各自代价与推荐。

## 2 约束（都是既有裁定，不是偏好）

| # | 约束 | 出处 |
|---|---|---|
| C1 | 队列（`backend.<id>.queue`）**见到即拒** | `profile/run_plan.hpp` 文档 · B1 |
| C2 | **计划就是脚本**；不带计划 ⇒ `plan-missing` | 同上 |
| C3 | `window.*` **不是脚本可见项**（用户裁决）；两条机检强制 | `contract/script_surface.hpp` |
| C4 | op 的**参数约束**由 `pipeline::plan_gate` 执行 | `contract/op_registry.hpp` |
| C5 | 脚本只能碰 `script_surface` 的名字（`backend` / `writeop` / `copyop` / `settle` / `handoff` / `close` / `cfg`） | 同 C3 |
| C6 | 脚本调用被**记录**（staged）；**尚无**「op → 步骤」的映射 | `script/staged_plan.hpp` 实测 |

## 3 两个形态

### 形态 A：三步成为**脚本动作**

`script_surface` 加三行（`Call`，`window_allowed=true`）⇒ Lua 直接写 `disenforce_selinux(h)` · `credential_forgery(h)` · `seccomp_bypass(h)`。

- **代价**：**与 C3 直接冲突** ⇒ 必须由用户**重新裁决 C3** 并留沿革；`script_surface` 的冻结判据与两条机检要同批改写。
- **收益**：Lua 对顺序的表述**最直接**；顺序完全在 Lua 手里，**含「只跑其中一步」** —— 这也是 A3-2 门禁里出现过的用法。

### 形态 B：三步是 `backend` 的**参数**

Lua 只调 `backend{ … }`，步序写在参数里（例如 `steps=[…]`）⇒ 顺序由 Lua **陈述**、由 gate **校验**、由 backend **执行**。

- **代价**：与 C3 **不冲突**；但要新增**参数形态**（参数目前挂在 op 上，而 `backend` 是脚本面名字 —— 两套名字体系的衔接需要设计）。
- **收益**：改动最小；语义与「队列退休」一致（顺序从文档里的队列搬到脚本里的参数）。

## 4 推荐

**形态 B** —— 理由：① 它是**唯一不与既有裁决冲突**的形态（不需要重新裁决 C3，少一次越权）；
② 它把顺序放在**一处**（脚本的参数），而 A 会同时把三步变成**脚本面动作** ⇒ 脚本面从 7 个名字涨到 10 个，冻结判据与两条机检都要重写；
③ **但 B 不能只跑其中一步**（三选一 ⇒ 只能整链）。

⇒ 因此若需要「**单独跑某一步**」（A3-2 门禁里有过这种用法），则必须选 **A** ⇒
**这是一条必须由用户回答的功能问题**，不是设计者能替答的。

## 5 落地清单（选定后同批）

| 件 | A | B |
|---|---|---|
| `contract/script_surface.hpp` | 加三行 + 改冻结判据 | 不改 |
| 两条机检（`window.` 前缀不得进脚本面） | 同批改写 | 不改 |
| `contract/op_registry.hpp` | 三行 op 与三步**对齐** | `backend` 的新参数进 `ParamSpec` |
| `pipeline/plan_gate.hpp` | 校验三步的顺序合法性 | 校验参数里的顺序 |
| `steps/step_runner.hpp` | **不变**（接一个 order、走完） | **不变** |
| ADR | 新增沿革（C3 被取代） | 引用 ADR-0011 即可 |

## 6 不在本设计内（写明不猜）

- 模版生成 Lua 的**时机**（构建期/导入期）与生成物是否仍需过 gate —— ADR-0011 §4 已列为 open；
- `LegacyProfileConverter` 的具体改造 —— 属 Kotlin 车道，需单独设计（本设计只定「顺序说什么」，不定「谁生成」）。
