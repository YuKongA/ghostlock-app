# 附件：target/copyop 的目标形态与「禁止名字→值表」（2026-10-08）

> 分析类（≤8192 B）。**配套**：[lua-surface-design](lua-surface-design.md)（§1 表 · 开头规则句）· [示例](a6-example-full-flow.md) · [A6 待批单](../plan/A6-baseline-approval.md)。来源：用户裁定「**至于 `target = "selinux_enforcing"` 中 `selinux_enforcing` 代表什么？是不是要 C++ 里解析成别的参数？禁止这样**」。

## §C1 规则（完整表述）

> **C++ 不得持有「名字 → 值」的映射表**。名字**只允许两类场合**：
> ① **标识符 / dispatch**（`route` 名 / `terminal` 名 / op 名）—— 它们**选的是实现**，词表权威在 **contract / profile**，且有 **manifest 对拍**；
> ② **数据面键路径**（`cfg.….offset.x`）—— 取的是**值**，值**来自 profile**。
> 除此之外，任何「**字符串进去、C++ 换成别的参数**」**一律禁止**。

- **撤回**：示例旧版把 `target` 写成裸符号名（`"selinux_enforcing"`）是**设计缺陷**（在 C++ 造了**第二权威**、把接口藏在字符串里）⇒ **撤回**，改为 §C2 形态。

## §C2 `writeop` / `copyop` 的目标形态

| 条目 | 参数（新） | 说明 |
|---|---|---|
| **`b:writeop{target, value, width}`** | **`target = { kind = <机制枚举>, value = <u64，来自数据面> }`** | `value` **必须来自 `cfg`（数据面）**；`width` 必填 |
| **`b:copyop{src, dst, width}`** | **`src`/`dst` = `{ kind = <机制枚举>, value = <u64，来自数据面> }`** | 宽度 = profile 声明的结构尺寸 |

- **`kind` 枚举（建议，待用户确认）**：**`kernel_offset`**（内核符号/字段偏移 ⇒ 运行期加**基址**）· **`absolute`**（已是绝对地址）· **`direct_map`**（direct-map 侧地址）。
- **`kind` 属机制常量**（可写进 C++），**须注释说明「为何不是调参」**；**`value` 不许硬编码**（缺失 ⇒ 具名拒绝，不兜底）。

## §C3 运行期换算 vs 名字解析（**禁止后者**）

- **允许（机制换算）**：**基址（KASLR）等运行期事实由 runtime 在窗口内补** —— 例：`kind = kernel_offset` ⇒ `target = 基址 + value`（**纯算术**，无查表）。
- **禁止（名字解析）**：runtime **不得做任何「名字 → 值」解析**（无符号表、无字段名表、无别名表）。
- **与 stage 的关系**：计划在**窗口前** stage ⇒ 计划**只能表达「偏移 + 目标形态」**，**不能**表达最终绝对地址（绝对地址是运行期事实）。

## §C4 裸名字全域审计（逐条判定）

| 处 | 裸名字 | 判定 | 处理 |
|---|---|---|---|
| `backend("cve_2026_43499")` / `"cve_2026_43284"` | backend id | **① 允许**（dispatch，词表权威 = contract） | 保留 |
| `backend(..., {route = "select_stack"})` / `"tcp_zerocopy"` / `"multicast_waiter"` | route 名 | **① 允许**（dispatch，有 manifest 对拍） | 保留 |
| `b:handoff{ terminal = "root_child" }` / `"umh_forward"` | terminal 名 | **① 允许**（dispatch） | 保留 |
| `cfg.backend.cve_2026_43499.execution.race.*` | 数据面键路径 | **② 允许**（取值，值来自 profile） | 保留 |
| `handoff{ payload_ref = … }` | 交付物引用 | **① 允许**（标识符；payload 由 Kotlin resolve） | 保留 |
| `handle-conflict` / `handle-stale` / `surface-unknown-name` / `run-form-invalid` | 拒绝码 | **① 允许**（标识符） | 保留 |
| **`writeop{target = "selinux_enforcing"}`** / `"seccomp_mode"` / `"pagecache_slot"` | **字段名当 target** | **❌ 违规**（名字→值解析） | **改为 `{kind=…, value=cfg.…}`** |
| **`copyop{src="cred_template", dst="cred_target"}`** | **符号名当 src/dst** | **❌ 违规** | **改为 `{kind=…, value=cfg.…}`** |

- **审计范围与方法**：`grep -ohE '"[a-z0-9_.]+"' docs/analysis/a6-example-full-flow.md docs/analysis/lua-surface-design.md`（本次实测命中 11 个不同字面量，上表逐条判定）⇒ **违规 5 处**（含 43284 那处），**其余全部属 ①/②**。
