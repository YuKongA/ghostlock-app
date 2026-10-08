# A6 基线待批单（一页 · 2026-10-08）

> 用途：把 **A6（Lua 脚本计划）开工所需的全部批准项**收在一页；**批准后即可进入 A6 实现批**。计划类（≤16 KiB）。
> 权威件：[lua-surface-design](../analysis/lua-surface-design.md) · [ADR-0008/0009/0010](../analysis/adr/) · [rewrite-plan-v2](rewrite-plan-v2.md)（A6）· [PENDING-DECISIONS](PENDING-DECISIONS.md)。

## 1. 待批内容（8 项）

| # | 项 | 内容 | 权威件 |
|---|---|---|---|
| 1 | **引擎与 vendoring** | **Lua 5.5.1** vendored 到 `src/lib/lua/`（`PROVENANCE.md`：398 643 B · sha256 前 16 `1c4b4068…` · 63/63 逐字节相同）；体积 ≈ **119 068 B ≈ 2.40%** | ADR-0008 |
| 2 | **沙箱预算参数** | **不注册** `io`/`os`/`package`/`debug`/`load`/`dofile`/`require`；指令预算 · 内存硬上限 · 栈深 · **单脚本 ≤64 KB** · 脚本**只在窗口外**运行（**5.5 API 逐项复核 = 待办，A6 批内**） | ADR-0008 · P5 规格 |
| 3 | **暴露面表** | 脚本可见 = `backend(id)` · `b:writeop{target,value,width?}`（`target ∈ profile 符号名`）· `b:settle{us}` · `b:handoff{terminal}` · **`cfg`（只读快照）**；**表外名字 ⇒ `surface-unknown-name`**（默认拒绝） | [lua-surface-design](../analysis/lua-surface-design.md) §1 |
| 4 | **单遍 stage** | 跑脚本（窗口外一次）⇒ **staged 计划** ⇒ `plan_gate` **校验 staged 产物**（不重跑脚本）⇒ **按相位执行**；**逐调用边界校验保留** | 同上 §3 |
| 5 | **`cfg` 快照** | stage 前注入的**只读快照**；脚本只能读字段；**不属句柄面**、不产生句柄 | 同上 §1 · ADR-0010 |
| 6 | **句柄边界** | `{kind,index,generation}` · **有界 64** · **POD / 无 `__gc`** · **只由宿主探针在窗口外产生** · 不得自造/延长/跨域 | ADR-0010 |
| 7 | **三条 ADR 修订点** | 0008 = 单遍 stage + 5.5 沙箱参数待办 · 0009 = 「用户脚本正确性 = 用户责任」= **`TD-RW2-1`** · 0010 = **`cfg` 不属句柄面** | 三份 ADR 修订段 |
| 8 | **`run` 双形态 + 收尾声明式** | `run{argv\|shell, capture?, timeout_ms?}`（**argv 与 shell 二者必居其一**，违规 ⇒ **`run-form-invalid`**）· 相位 = **terminal（root 后）** · 窗口内否；收尾 = `terminal{on_success, always, on_failure}` **声明式**（内核兜底留 C++；旧 `root_cmd` 模板被取代）⇒ **`TD-RW2-2`** | [lua-surface-design](../analysis/lua-surface-design.md) §1/§4.1 |

## 2. 现状（2026-10-08）

- **已就位**：`src/lib/lua/`（5.5.1 + `PROVENANCE.md`）· 暴露面设计与 UML（§2.8/§3.4/§4.5 已同步 stage/相位/cfg）。
- **未开始**：A6 实现（沙箱接线、`surface` 表、stage、gate 校验 staged、句柄表）。
- **前置未决**：待裁三条（`settle` 进窗口 / `width` 缺省 / 脚本承载方式）；**5.5 API 逐项复核**（沙箱参数）。

## 3. 判据（批准后 A6 的验收）

- **白盒**：沙箱**符号级证据**（产物无 `io/os/package/debug/load/dofile/require`）；`surface` 表与绑定**同源**（注册集合 == 表；名字只定义一次）；句柄表**有界 64 + 代际**。
- **黑盒**：表外名字 ⇒ `surface-unknown-name`；缺参数 ⇒ 具名拒绝（**不兜底**）；staged 计划被篡改 ⇒ gate 拒绝。
- **证伪（两条，按 R42 执行）**：① 放开 `load`/`dofile` 之一 ⇒ 指定沙箱测试**必须失败**；② 去掉代际检查 ⇒ 旧句柄用例**必须失败**。
- **极端值**：空脚本 · 语法错 · 超预算 · 表超深 · 64 句柄用满 · 跨插件句柄 · 脚本内 `queue` 残留。

## 4. 不批的后果

- **A6 无法开工**（沙箱与暴露面同源绑定、句柄模型都是实现前提）⇒ **P5 相关项全部顺延**；
- `plan_gate` 无 staged 对象可校 ⇒ **窗口相位执行缺乏校验依据**；
- 引擎版本/vendoring 虽已裁定，但**沙箱参数未批** = 不得接线 ⇒ A6 只能停在"源码就位"状态。

## 5. `TD-RW2-1` 登记（纪律放宽）

| 项 | 内容 |
|---|---|
| 放宽 | **不为脚本逻辑错误做静态证明**（用户裁定：**用户脚本正确性 = 用户责任**） |
| **保留** | **窗口内零 Lua** · **逐调用边界校验** · **`plan_gate` 校验 staged** · **预算**（指令/内存/64 KB） |
| 代价 | 用户脚本改坏**可能 panic**（本层不拦截，如实登记） |

**另：`TD-RW2-2`（`run` 双形态）** —— 允许 `shell` ⇒ **命令级白名单失效**；**剩余界** = **相位**（仅 terminal、root 后）+ **预算**（指令/内存/`timeout_ms`）+ **审计**（命令与输出入日志）；**代价** = root 后**可执行任意命令**；**偿还时机 = 首个真机门禁（G2）后复评**。
| 偿还时机 | **首个真机门禁（G2）后复评** |

## 6. 一句批准项（可直接转给用户）

> **请批准 A6 基线：Lua 5.5.1（已 vendor、来源可核）+ 沙箱预算参数白名单 + 暴露面表（`backend`/`writeop`/`settle`/`handoff`/`cfg`/**`run`**，表外拒绝）+ 单遍 stage（跑脚本→gate 校验 staged→按相位执行）+ `cfg` 只读快照 + 句柄边界（有界 64 / POD / 仅宿主探针产生）+ **`run` 双形态与声明式收尾**（`argv`/`shell` 二选一，`terminal{on_success, always, on_failure}`，内核兜底留 C++），并登记 **`TD-RW2-1`**（用户脚本正确性 = 用户责任；保留窗口内零 Lua / 逐调用校验 / gate 校验 staged / 预算）与 **`TD-RW2-2`**（允许 `shell` ⇒ 以相位+预算+审计为剩余界；两项偿还时机 = 首个真机门禁后复评）。**
