# ADR-0006：terminal 归属 —— **实现下放到 backend，词汇保留**

- 状态：Accepted（维护者授权，2026-10-05）
- 索引：[README.md](README.md)
- 日期：2026-10-05
- 基线：`be7b58e`（S4-R6a）
- 相关：ADR-0004（组合轴 R18–R21、稀疏 triple）、`docs/analysis/s4-r6b-composition-design.md`（token）、
  `src/core/pipeline/component_catalog.hpp`（装配权威）

## 背景（Context）

维护者提问：backend 与 terminal 的耦合应是「像现在这样拆开」，还是「**干脆让 backend 包含 terminal，即使重复写**」。
取证结论：**当前是名义两轴、实际已泄漏**——

| 证据 | 含义 |
|---|---|
| `terminal/root_child.cpp` `#include "backend/cve_2026_43499_state.hpp"` 与 `backend/cve_2026_43499/route/route_api.hpp`，并在 7 处读 `cve43499_state(...).profile.handoff_*` | `root_child` **本就是 43499 的模块** |
| `terminal/root_script.cpp` 同样读 43499 的 `safe_mode` | 脚本生成与 43499 状态耦合（1 处） |
| `terminal/umh_forward.cpp` **无任何 backend include** | 它是**真正的中性件** |
| `main.cpp` `#include "backend/cve_2026_43284/session_frame.hpp"`；Kotlin `requiresSessionFrame(backend)` | **入口与会话帧按 backend 分支**（第二处泄漏） |
| 中性件规模：`root_program.hpp` 17 行、`umh_command.hpp` 35、`terminal_input.hpp` 110、`handoff_probe.*` ~5 KB（无 backend 依赖）、`root_script.cpp` ~11 KB（仅 1 处耦合） | **重头是可共享的**，策略胶水很薄 |

稀疏 triple 给了**编译期组合**，但两个 terminal **不可互换**；每个组合是 1:1 的 backend 专用胶水。
R6b 又把 path 放进 `backend.<id>.steps`（token）—— 两轴在**收敛为一轴**。

## 决策（Decision）

1. **词汇保留**：`contract::TerminalKind` 与 path 名（`rootchild` / `shizuku` / `umh`）**继续作为对用户可见的词汇**（token、UI、profile、诊断）。
2. **实现下放**：terminal 的**实现**归属 backend（`backend/<id>/terminal/**`）；不再要求实现中性。
3. **共享件抽出**（中性层，禁止依赖任何 backend）：`root_program.hpp`、`root_script.*`（脚本**文本生成**，改为参数化、不读 backend state）、
   `handoff_probe.*`、`umh_command.hpp`、`terminal_input.hpp`。
4. **装配权威收敛**：`component_catalog` 由 `(backend, steps, terminal)` → **`(backend, steps_token)`**（token 内含 path）；`Pipeline<Backend, Token>`。
   与 R6b 同批或紧随（T5）。
5. **入口去 backend 化**：`main.cpp` **不得** include 具体 backend 头；backend 经**钩子**提供侧信道/会话帧读取（`read_side_channel(...)`）。
6. **重估判据**：若出现「同一 terminal 实现被 **≥2 个 backend 原样复用**」→ 把该实现**上提**为公共 terminal，并定义中性契约（requirements/provisions：payload 就绪 / 权限状态 / 会话密钥 / 写入结果 / 清理）。

## 后果（Consequences）

**正面**：消除两处跨轴泄漏；catalog 从三元组简化为 `(backend, token)`；路径自包含、调试直观；重复成本仅限每组合几十行策略胶水。
**负面**：若将来真出现 3×3 的纯正交复用，会有策略代码重复（按 roadmap 判断收益低于成本）。
**风险**：共享件边界必须一次切净，否则退化为隐式耦合；`root_script` 参数化时不得改变生成文本（脚本文本是攻击路径的一部分）。

## 执行批次

| 批 | 内容 | 门禁 |
|---|---|---|
| **T0** | 本 ADR + 计划同步 | 文档 |
| **T1** | 抽通用件：切断 `root_script.*` 对 43499 state 的读取（参数化），确认 `terminal/` 其余通用件无 backend 依赖 | host/NDK/lint + 真机 43499 |
| **T2** | 43499 terminal 下放到 `backend/cve_2026_43499/terminal/`（root_child + 43499 专用胶水） | host/NDK/lint + **真机 43499 冷启** |
| **T3** | 43284 侧下放（`umh_forward` 保持中性；会话帧/LKM 胶水归 backend） | host/NDK/lint + **真机 43284 app-call** |
| **T4** | 入口去 backend 化：`main.cpp` 改走 backend 钩子（去 `session_frame.hpp` include） | + 真机双门禁 |
| **T5** | catalog 收敛 `(backend, token)`；`TerminalKind` 降级为 path 词汇（**与 R6b 合并**） | + 真机 3 route 各一次 |

## 备注（与 R6 的关系）

- T1–T4 **不依赖** R6b 的 token（wire 不变）；可与 R6b 并行，但为了避免同时改 catalog，**T5 与 R6b 合并执行**；
- 本 ADR 不改变 wire：`schema == 3` 不变；token 的引入是 R6b 的事。
